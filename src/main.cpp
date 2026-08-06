#define GLFW_INCLUDE_NONE
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_node_editor.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "core/NodeFactory.h"
#include "core/GLUtil.h"
#include "core/GraphNode.h"
#include "core/FilterDefs.h"
#include "core/BlendModes.h"
#include "core/Transport.h"
#include "core/Modulation.h"
#include "nodes/ImageSourceNode.h"
#include "nodes/ShapeNode.h"
#include "nodes/FormulaNode.h"
#include "nodes/FilterNode.h"
#include "nodes/BlendNode.h"
#include "nodes/LayerStackNode.h"
#include "nodes/TextNode.h"
#include "nodes/FitNode.h"
#include "nodes/VideoSourceNode.h"
#include "nodes/NoiseNode.h"
#include "nodes/ResynthNode.h"
#include "nodes/MacroNodes.h"
#include "nodes/CurvesNode.h"
#include "nodes/RemoveBgNode.h"
#include "nodes/RampNode.h"
#include "nodes/AnalyzeNodes.h"
#include "nodes/Geometry3DNodes.h"
#include "nodes/DrawNode.h"
#include "nodes/FeedbackNodes.h"
#include "nodes/SwitcherNode.h"
#include "nodes/ModulatorNodes.h"
#include "nodes/OutputNode.h"

namespace ed = ax::NodeEditor;

namespace
{
   // Every node renders its output at this size, square, above its params.
   const float kPreviewSize = 190.0f;
   const float kParamWidth = 168.0f;
   const float kPinRadius = 7.0f;
   const float kPinHit = 20.0f; // generous click target - small dots were unhittable


   std::vector<GraphNode> gNodes;
   int gNextIndex = 0;
   ed::EditorContext* gEditor = nullptr;
   GraphNode* gSelfTestFeeder = nullptr;
   ImVec2 gSpawnPos(0.0f, 0.0f);

   struct LinkInfo
   {
      int id = 0;
      int srcPin = 0;
      int dstPin = 0;
   };
   const int kLinkIdBase = 4000000; // far above any node/pin id
   std::vector<LinkInfo> gLinks;

   const LinkInfo* FindLink(int id)
   {
      for (const LinkInfo& link : gLinks)
      {
         if (link.id == id)
            return &link;
      }
      return nullptr;
   }
   bool gSnapToGrid = true;
   float gGridSnap = 20.0f;
   bool gRequestFitView = false;
   float gZoomSensitivity = 0.5f;
   bool gHoveringItem = false;   // last frame: cursor over a node/pin/link
   bool gPanWithLeft = false;    // current left-drag is a canvas pan, not a select
   ImVec2 gDragTestNodeScreen(0.0f, 0.0f);
   ImVec2 gDragTestNodePos(0.0f, 0.0f);
   ImVec2 gTestMouse(0.0f, 0.0f);
   ImVec2 gDragTestViewAnchor(0.0f, 0.0f);

   // ---- deferred dropdown -------------------------------------------------
   // ImGui combos opened inside a node get clipped and mis-scaled by the node
   // editor's canvas transform. Instead a node draws a plain button, records
   // what it wants to show, and the popup is rendered once per frame outside
   // the canvas (inside ed::Suspend) as a normal, scrollable ImGui popup.
   struct DropdownRequest
   {
      const std::vector<std::string>* options = nullptr;
      std::function<void(int)> onSelect;
      int current = 0;
      bool justOpened = false;
   };
   DropdownRequest gDropdown;

   // Same story for ImGui's colour picker: opened inside a node it inherits the
   // canvas transform and the hue bar / sliders stop tracking the cursor. Route
   // it through a popup rendered outside the canvas instead.
   struct ColorRequest
   {
      float* target = nullptr;
      const INode* owner = nullptr; // revalidated each frame; nodes can be deleted
      std::string label;
      bool justOpened = false;
   };
   ColorRequest gColor;
   ImVec4 gColorPickerRect(0, 0, 0, 0); // x, y, w, h of the picker widget on screen
   FormulaNode* gFormulaEditor = nullptr;
   bool gFormulaEditorOpen = false;
   bool gHelpOpen = false;

   // Files dropped on the window, consumed on the next frame so the spawn can
   // happen inside the editor where canvas coordinates are meaningful.
   std::vector<std::string> gDroppedFiles;
   ImVec2 gDropPos(0.0f, 0.0f);

   bool HasExtension(const std::string& path, const std::vector<std::string>& exts)
   {
      size_t dot = path.find_last_of('.');
      if (dot == std::string::npos)
         return false;
      std::string ext = path.substr(dot + 1);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      for (const std::string& e : exts)
      {
         if (ext == e)
            return true;
      }
      return false;
   }

   void OnFilesDropped(GLFWwindow*, int count, const char** paths)
   {
      gDropPos = ImGui::GetMousePos();
      for (int i = 0; i < count; i++)
         gDroppedFiles.push_back(paths[i]);
   }

   void DropdownButton(const char* label, const std::vector<std::string>& options,
                       int current, std::function<void(int)> onSelect)
   {
      if (options.empty())
         return;
      int safeCurrent = std::max(0, std::min(current, (int)options.size() - 1));
      std::string caption = options[safeCurrent] + "##" + label;
      if (ImGui::Button(caption.c_str(), ImVec2(kParamWidth, 0)))
      {
         gDropdown.options = &options;
         gDropdown.onSelect = std::move(onSelect);
         gDropdown.current = safeCurrent;
         gDropdown.justOpened = true;
      }
      ImGui::SameLine();
      // anything after "##" is an ImGui uniquifier, not part of the visible name
      std::string shown(label);
      size_t hash = shown.find("##");
      if (hash != std::string::npos)
         shown = shown.substr(0, hash);
      ImGui::TextDisabled("%s", shown.c_str());
   }

   void ColorSwatch(const char* label, float* col, const INode* owner)
   {
      ImGui::PushID(label);
      if (ImGui::ColorButton(label, ImVec4(col[0], col[1], col[2], 1.0f),
                             ImGuiColorEditFlags_NoTooltip, ImVec2(38, 0)))
      {
         gColor.target = col;
         gColor.owner = owner;
         gColor.label = label;
         gColor.justOpened = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", label);
      ImGui::PopID();
   }

   // ---- modulatable parameters --------------------------------------------
   // Every slider that can be driven by a modulator goes through ModSlider. It
   // draws an input pin beside the control, registers the parameter for this
   // frame so the modulator can write into it, and shows the live value (and
   // locks the slider) while something is patched in.
   int gCurrentNodeIndex = -1;
   int gParamCounter = 0;
   std::set<std::pair<int, int>> gTypedParam;                 // params showing a text field
   // Param pins declared this frame. A node with its params collapsed declares
   // none, and emitting a link to an undeclared pin makes the editor treat the
   // link as dead and delete it - which silently dropped the modulation.
   std::set<int> gDrawnParamPins;
   // Nodes to select next frame; they do not exist in the editor until they
   // have been drawn once, so selection has to wait a frame.
   std::vector<int> gPendingSelect;
   std::pair<int, int> gTypedParamJustOpened(-1, -1);
   std::vector<int> gParamPinsThisFrame;

   void BeginNodeParams(int nodeIndex)
   {
      gCurrentNodeIndex = nodeIndex;
      gParamCounter = 0;
   }

   bool ModSlider(const char* label, float* value, float minV, float maxV, const char* fmt = "%.3f")
   {
      const int nodeIndex = gCurrentNodeIndex;
      const int paramIndex = gParamCounter++;

      ParamRef ref;
      ref.nodeIndex = nodeIndex;
      ref.paramIndex = paramIndex;
      ref.value = value;
      ref.minValue = minV;
      ref.maxValue = maxV;
      ref.name = label;
      Modulation::Instance().RegisterParam(ref);

      const int pinId = nodeIndex * GraphNode::kStride + GraphNode::kParamBase + paramIndex;
      const bool modulated = Modulation::Instance().IsModulated(nodeIndex, paramIndex);
      gDrawnParamPins.insert(pinId);

      ImGui::PushID(paramIndex + 5000);

      ed::BeginPin(pinId, ed::PinKind::Input);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
      ImVec2 p = ImGui::GetCursorScreenPos();
      const float box = 14.0f;
      ImGui::Dummy(ImVec2(box, box));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + box * 0.5f, p.y + box * 0.5f);
      dl->AddCircleFilled(c, 4.0f, modulated ? IM_COL32(255, 190, 90, 255) : IM_COL32(95, 100, 120, 255));
      ed::EndPin();
      ImGui::SameLine(0.0f, 4.0f);

      // Double-clicking swaps the slider for a text field so an exact value can
      // be typed. ImGui's built-in Ctrl+click does this too, but double-click is
      // what people reach for.
      const std::pair<int, int> editKey(nodeIndex, paramIndex);
      bool typing = gTypedParam.count(editKey) > 0;

      bool changed = false;
      if (typing && !modulated)
      {
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         if (gTypedParamJustOpened == editKey)
         {
            ImGui::SetKeyboardFocusHere();
            gTypedParamJustOpened = std::pair<int, int>(-1, -1);
         }
         changed = ImGui::InputFloat(label, value, 0.0f, 0.0f, fmt,
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll);
         if (changed || ImGui::IsItemDeactivated())
            gTypedParam.erase(editKey);
      }
      else if (modulated)
      {
         // value is driven externally; show it read-only so it is obvious why
         // dragging does nothing
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.24f, 0.08f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.95f, 0.72f, 0.32f, 1.0f));
         float shown = *value;
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         ImGui::SliderFloat(label, &shown, minV, maxV, fmt, ImGuiSliderFlags_NoInput);
         ImGui::PopStyleColor(2);
      }
      else
      {
         ImGui::SetNextItemWidth(kParamWidth - box - 4.0f);
         changed = ImGui::SliderFloat(label, value, minV, maxV, fmt);
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            gTypedParam.insert(editKey);
            gTypedParamJustOpened = editKey;
         }
      }

      ImGui::PopID();
      return changed;
   }

   // Integer parameter that is still modulatable. The backing float lives in a
   // std::map so its address stays valid for the whole frame (node references are
   // stable), which matters because the modulation pass writes through that pointer.
   std::map<std::pair<int, int>, float> gIntParamStore;

   bool ModSliderInt(const char* label, int* value, int minV, int maxV)
   {
      const std::pair<int, int> key(gCurrentNodeIndex, gParamCounter);
      float& slot = gIntParamStore[key];
      if (!Modulation::Instance().IsModulated(key.first, key.second))
         slot = (float)*value;

      bool changed = ModSlider(label, &slot, (float)minV, (float)maxV, "%.0f");
      *value = (int)(slot + 0.5f);
      return changed;
   }

   // ImGui's Separator / SeparatorText span the available content width, and
   // inside a node that width is unbounded - the rule shot off across the whole
   // canvas. Draw our own, clamped to the node's preview width.
   void NodeSeparator(const char* label = nullptr)
   {
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float y = origin.y + 6.0f;
      const ImU32 col = IM_COL32(78, 82, 100, 255);

      if (label != nullptr && label[0] != '\0')
      {
         const ImVec2 textSize = ImGui::CalcTextSize(label);
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + 10.0f, y), col);
         dl->AddText(ImVec2(origin.x + 16.0f, origin.y), IM_COL32(150, 156, 180, 255), label);
         const float lineStart = origin.x + 22.0f + textSize.x;
         if (lineStart < origin.x + kPreviewSize)
            dl->AddLine(ImVec2(lineStart, y), ImVec2(origin.x + kPreviewSize, y), col);
         ImGui::Dummy(ImVec2(kPreviewSize, textSize.y));
      }
      else
      {
         dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + kPreviewSize, y), col);
         ImGui::Dummy(ImVec2(kPreviewSize, 8.0f));
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
   }

   const std::vector<std::string>& AlignOptions()
   {
      static const std::vector<std::string> kAlign = { "Left", "Center", "Right", "Justified" };
      return kAlign;
   }

   // Small eye toggle. Drawn rather than typed: the UI font has no eye glyph,
   // and an emoji would not render in a non-emoji face.
   bool EyeToggle(bool shown)
   {
      const float w = 26.0f;
      const float h = 18.0f;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton("##eye", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(origin.x + w * 0.5f, origin.y + h * 0.5f);
      ImU32 col = hovered ? IM_COL32(235, 240, 255, 255)
                          : (shown ? IM_COL32(150, 190, 255, 255) : IM_COL32(120, 124, 140, 255));

      // almond outline: two arcs meeting at the corners
      const float rx = 9.0f, ry = 5.5f;
      dl->PathClear();
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = -rx + 2.0f * rx * t;
         float y = -ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      for (int i = 0; i <= 16; i++)
      {
         float t = (float)i / 16.0f;
         float x = rx - 2.0f * rx * t;
         float y = ry * (float)sin(3.14159f * t);
         dl->PathLineTo(ImVec2(c.x + x, c.y + y));
      }
      dl->PathStroke(col, ImDrawFlags_Closed, 1.6f);

      if (shown)
         dl->AddCircleFilled(c, 2.6f, col);
      else
         dl->AddLine(ImVec2(c.x - rx, c.y + ry * 0.9f), ImVec2(c.x + rx, c.y - ry * 0.9f), col, 1.6f);

      return pressed;
   }

   // ---- pins --------------------------------------------------------------
   // Drawn inside a kPinHit-wide box so the clickable area is far larger than
   // the visible dot; connecting used to require pixel-perfect aim.
   void DrawPin(int pinId, ed::PinKind kind, const char* label, bool labelFirst = false)
   {
      ed::BeginPin(pinId, kind);
      ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));

      if (labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::TextDisabled("%s", label);
         ImGui::SameLine(0.0f, 4.0f);
      }

      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(kPinHit, kPinHit));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + kPinHit * 0.5f, p.y + kPinHit * 0.5f);
      dl->AddCircleFilled(c, kPinRadius, IM_COL32(150, 190, 255, 255));
      dl->AddCircle(c, kPinRadius, IM_COL32(20, 22, 30, 255), 0, 1.5f);

      if (!labelFirst && label != nullptr && label[0] != '\0')
      {
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::TextDisabled("%s", label);
      }
      ed::EndPin();
   }

   void RegisterNodes()
   {
      REGISTER_NODE(ImageSourceNode, Image Source, "Source");
      REGISTER_NODE(ShapeNode, Shape, "Source");
      REGISTER_NODE(FormulaNode, Formula, "Source");
      REGISTER_NODE(TextNode, Text, "Text");
      REGISTER_NODE(VideoSourceNode, Video, "Source");
      REGISTER_NODE(NoiseNode, Noise, "Source");
      REGISTER_NODE(RampNode, Ramp, "Source");
      REGISTER_NODE(GeometryNode, Geometry, "3D");
      REGISTER_NODE(Render3DNode, Render 3D, "3D");
      REGISTER_NODE(DrawNode, Draw, "Source");
      REGISTER_NODE(ResynthNode, Resynthesize, "Resynth");
      REGISTER_NODE(FitNode, Fit, "Compositing");
      REGISTER_NODE(CurvesNode, Curves, "Color");
      REGISTER_NODE(RemoveBgNode, Remove Background, "Mask");
      REGISTER_NODE(FeedbackNode, Feedback, "Feedback");
      REGISTER_NODE(TrailsNode, Trails, "Feedback");
      REGISTER_NODE(ReactionDiffusionNode, Reaction Diffusion, "Feedback");
      REGISTER_NODE(BlendNode, Blend, "Compositing");
      REGISTER_NODE(LayerStackNode, Layer Stack, "Compositing");
      REGISTER_NODE(SwitcherNode, Switcher, "Compositing");
      REGISTER_NODE(OutputNode, Output, "Output");
      REGISTER_NODE(LFONode, LFO, "Modulators");
      REGISTER_NODE(RandomNode, Random, "Modulators");
      REGISTER_NODE(PatternNode, Pattern, "Modulators");
      REGISTER_NODE(MathNode, Math, "Modulators");
      REGISTER_NODE(MacroKnobNode, Macro Knob, "Modulators");
      REGISTER_NODE(MacroXYNode, Macro XY, "Modulators");
      REGISTER_NODE(ImageAnalyzeNode, Image Analyze, "Modulators");
      REGISTER_NODE(AudioFileNode, Audio File, "Modulators");
      REGISTER_NODE(AudioAnalyzeNode, Audio Analyze, "Modulators");

      // Every entry in the filter table becomes its own spawnable node type,
      // all sharing FilterNode. `def` is a reference into the static table, so
      // capturing it by pointer is safe for the process lifetime.
      for (const FilterDef& def : GetFilterDefs())
      {
         const FilterDef* defPtr = &def;
         NodeFactory::Instance().Register(
            def.name,
            [defPtr]() -> INode* { return FilterNode::CreateFor(*defPtr); },
            def.category);
      }
   }

   IModulator* ModulatorForOutput(INode* node, int outputIndex)
   {
      if (node == nullptr)
         return nullptr;
      if (IModulator* specific = node->ModulatorOutput(outputIndex))
         return specific;
      return outputIndex == 0 ? dynamic_cast<IModulator*>(node) : nullptr;
   }

   GraphNode* FindNodeByIndex(int index)
   {
      for (GraphNode& gn : gNodes)
      {
         if (gn.index == index)
            return &gn;
      }
      return nullptr;
   }

   // How many image inputs a node exposes (drives pin count + link routing).
   int InputCountFor(const GraphNode& gn)
   {
      if (dynamic_cast<LayerStackNode*>(gn.node.get()) != nullptr)
         return LayerStackNode::kSlots;
      if (dynamic_cast<SwitcherNode*>(gn.node.get()) != nullptr)
         return SwitcherNode::kSlots;
      // Math takes two modulator cables rather than images, but they are still
      // ordinary input pins as far as the editor is concerned.
      if (dynamic_cast<MathNode*>(gn.node.get()) != nullptr)
         return 2;
      if (dynamic_cast<BlendNode*>(gn.node.get()) != nullptr)
         return 2;
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
         return filter->Def().inputs;
      if (dynamic_cast<FitNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ResynthNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<CurvesNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<RemoveBgNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<DrawNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr)
         return Render3DNode::kSlots;
      if (dynamic_cast<FeedbackNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<TrailsNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<ReactionDiffusionNode*>(gn.node.get()) != nullptr)
         return 1;
      if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
         return 1;
      return 0; // sources and modulators have no image inputs
   }

   ImageCable* CableFor(GraphNode& gn, int slot)
   {
      if (auto* stack = dynamic_cast<LayerStackNode*>(gn.node.get()))
         return (slot >= 0 && slot < LayerStackNode::kSlots) ? &stack->Input(slot) : nullptr;
      if (auto* sw = dynamic_cast<SwitcherNode*>(gn.node.get()))
         return (slot >= 0 && slot < SwitcherNode::kSlots) ? &sw->Input(slot) : nullptr;
      if (auto* blend = dynamic_cast<BlendNode*>(gn.node.get()))
         return slot == 0 ? &blend->InputA() : &blend->InputB();
      if (auto* filter = dynamic_cast<FilterNode*>(gn.node.get()))
      {
         if (slot == 0)
            return &filter->Input();
         return (slot == 1 && filter->Def().inputs > 1) ? &filter->Input2() : nullptr;
      }
      if (auto* fit = dynamic_cast<FitNode*>(gn.node.get()))
         return slot == 0 ? &fit->Input() : nullptr;
      if (auto* resynth = dynamic_cast<ResynthNode*>(gn.node.get()))
         return slot == 0 ? &resynth->Input() : nullptr;
      if (auto* curves = dynamic_cast<CurvesNode*>(gn.node.get()))
         return slot == 0 ? &curves->Input() : nullptr;
      if (auto* rbg = dynamic_cast<RemoveBgNode*>(gn.node.get()))
         return slot == 0 ? &rbg->Input() : nullptr;
      if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
         return slot == 0 ? &draw->Input() : nullptr;
      if (auto* an = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
         return slot == 0 ? &an->Input() : nullptr;
      if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
         return slot == 0 ? &geo->TextureInput() : nullptr;
      if (auto* fb = dynamic_cast<FeedbackNode*>(gn.node.get()))
         return slot == 0 ? &fb->Input() : nullptr;
      if (auto* trails = dynamic_cast<TrailsNode*>(gn.node.get()))
         return slot == 0 ? &trails->Input() : nullptr;
      if (auto* rd = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
         return slot == 0 ? &rd->Input() : nullptr;
      if (auto* out = dynamic_cast<OutputNode*>(gn.node.get()))
         return slot == 0 ? &out->Input() : nullptr;
      return nullptr;
   }

   GraphNode* SpawnNode(const std::string& typeName, const std::string& category,
                        float x = 0.0f, float y = 0.0f)
   {
      INode* node = NodeFactory::Instance().MakeNode(typeName);
      if (node == nullptr)
         return nullptr;

      GraphNode gn;
      gn.node.reset(node);
      gn.typeName = typeName;
      gn.category = category;
      gn.index = gNextIndex++;
      gn.spawnX = x;
      gn.spawnY = y;
      gNodes.push_back(std::move(gn));
      return &gNodes.back();
   }

   // Copies user-visible parameters between two nodes of the same type. Deliberately
   // field-by-field rather than a struct copy: the objects own GL texture/FBO handles
   // that must not be shared or double-freed.
   void CopyParams(INode* dstNode, INode* srcNode)
   {
      if (auto* d = dynamic_cast<ShapeNode*>(dstNode))
      {
         auto* s = static_cast<ShapeNode*>(srcNode);
         d->shapeType = s->shapeType;
         d->width = s->width;
         d->height = s->height;
         d->size = s->size;
         d->aspect = s->aspect;
         d->cornerRadius = s->cornerRadius;
         d->sides = s->sides;
         d->innerRatio = s->innerRatio;
         d->rotation = s->rotation;
         d->posX = s->posX;
         d->posY = s->posY;
         d->fillOpacity = s->fillOpacity;
         d->strokeWidth = s->strokeWidth;
         d->feather = s->feather;
         d->bgOpacity = s->bgOpacity;
         memcpy(d->fillColor, s->fillColor, sizeof(d->fillColor));
         memcpy(d->strokeColor, s->strokeColor, sizeof(d->strokeColor));
         memcpy(d->bgColor, s->bgColor, sizeof(d->bgColor));
      }
      else if (auto* d = dynamic_cast<FormulaNode*>(dstNode))
      {
         auto* s = static_cast<FormulaNode*>(srcNode);
         d->formula = s->formula;
         d->width = s->width;
         d->height = s->height;
         d->knobA = s->knobA;
         d->knobB = s->knobB;
         d->knobC = s->knobC;
         d->knobD = s->knobD;
         d->animate = s->animate;
         d->Apply();
      }
      else if (auto* d = dynamic_cast<TextNode*>(dstNode))
      {
         auto* s = static_cast<TextNode*>(srcNode);
         d->text = s->text;
         d->fontName = s->fontName;
         d->fontSize = s->fontSize;
         d->tracking = s->tracking;
         d->posX = s->posX;
         d->posY = s->posY;
         d->align = s->align;
         d->width = s->width;
         d->height = s->height;
         memcpy(d->color, s->color, sizeof(d->color));
      }
      else if (auto* d = dynamic_cast<LayerStackNode*>(dstNode))
      {
         auto* s = static_cast<LayerStackNode*>(srcNode);
         memcpy(d->modes, s->modes, sizeof(d->modes));
         memcpy(d->opacities, s->opacities, sizeof(d->opacities));
      }
      else if (auto* d = dynamic_cast<BlendNode*>(dstNode))
      {
         auto* s = static_cast<BlendNode*>(srcNode);
         d->ModeIndex() = s->ModeIndex();
         d->Mix() = s->Mix();
      }
      else if (auto* d = dynamic_cast<FilterNode*>(dstNode))
      {
         auto* s = static_cast<FilterNode*>(srcNode);
         for (size_t i = 0; i < d->Def().params.size() && i < s->Def().params.size(); i++)
         {
            for (int c = 0; c < 3; c++)
               d->SetParamValue(i, c, s->GetParamValue(i, c));
         }
      }
      else if (auto* d = dynamic_cast<ImageSourceNode*>(dstNode))
      {
         auto* s = static_cast<ImageSourceNode*>(srcNode);
         d->pathInput = s->pathInput;
         if (!s->LoadedPath().empty())
            d->Load(s->LoadedPath());
      }
   }

   // ---------------- per-node parameter UI ----------------

   void DrawImageSourceParams(ImageSourceNode* n)
   {
      if (ImGui::Button("Choose image...", ImVec2(kPreviewSize, 0)))
         n->LoadViaDialog();

      if (!n->LastError().empty())
      {
         // wrap pos is window-relative; passing a bare width put it left of the
         // cursor and wrapped every single character onto its own line
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s  (%dx%d)", file.c_str(), n->GetOutputWidth(), n->GetOutputHeight());
         ImGui::PopTextWrapPos();
      }
   }

   void DrawShapeParams(ShapeNode* n)
   {
      DropdownButton("shape", ShapeNode::ShapeNames(), n->shapeType,
                     [n](int i) { n->shapeType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("size", &n->size, 0.01f, 0.5f);
      ModSlider("aspect", &n->aspect, 0.2f, 3.0f);
      ModSlider("corner/thick", &n->cornerRadius, 0.0f, 0.3f);
      ModSliderInt("sides", &n->sides, 3, 20);
      ModSlider("inner ratio", &n->innerRatio, 0.05f, 1.0f);
      ModSlider("rotation", &n->rotation, -3.1416f, 3.1416f);
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      ColorSwatch("fill", n->fillColor, n);
      ModSlider("fill opacity", &n->fillOpacity, 0.0f, 1.0f);
      ModSlider("stroke width", &n->strokeWidth, 0.0f, 0.1f);
      ColorSwatch("stroke", n->strokeColor, n);
      ModSlider("feather", &n->feather, 0.0f, 0.1f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }

   void DrawFormulaParams(FormulaNode* n)
   {
      // The GLSL editor lives in its own window, not inline: ImGui multi-line
      // fields are child windows, and child windows inside the node canvas get
      // clipped away - which is why the box used to render empty.
      DropdownButton("preset", FormulaNode::PresetNames(), n->presetIndex,
                     [n](int i) { n->presetIndex = i; n->LoadPreset(i); });

      if (ImGui::Button("Edit GLSL...", ImVec2(kPreviewSize, 0)))
      {
         gFormulaEditor = n;
         gFormulaEditorOpen = true;
      }

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }

      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("uA", &n->knobA, 0.0f, 1.0f);
      ModSlider("uB", &n->knobB, 0.0f, 1.0f);
      ModSlider("uC", &n->knobC, 0.0f, 1.0f);
      ModSlider("uD", &n->knobD, 0.0f, 1.0f);
      ImGui::Checkbox("animate", &n->animate);
   }

   void DrawTextParams(TextNode* n)
   {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", n->text.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("text", buf, sizeof(buf)))
         n->text = buf;

      const std::vector<std::string>& fonts = TextNode::AvailableFonts();
      if (n->fontName.empty())
         n->fontName = fonts.front();
      int fontIdx = 0;
      for (int i = 0; i < (int)fonts.size(); i++)
      {
         if (fonts[i] == n->fontName)
            fontIdx = i;
      }
      DropdownButton("font", fonts, fontIdx, [n, &fonts](int i) { n->fontName = fonts[i]; });

      ModSlider("size", &n->fontSize, 8.0f, 300.0f);
      ColorSwatch("color", n->color, n);
      ModSlider("tracking", &n->tracking, -10.0f, 40.0f);
      ModSlider("pos x", &n->posX, 0.0f, 1.0f);
      ModSlider("pos y", &n->posY, 0.0f, 1.0f);
      DropdownButton("align", AlignOptions(), n->align, [n](int i) { n->align = i; });
      ModSlider("scale x", &n->scaleX, 0.1f, 4.0f);
      ModSlider("scale y", &n->scaleY, 0.1f, 4.0f);
      ImGui::Checkbox("word wrap", &n->wordWrap);
      if (n->wordWrap)
      {
         ModSlider("box width", &n->wrapWidth, 0.1f, 1.0f);
         ModSlider("box height", &n->wrapHeight, 0.1f, 1.0f);
         ImGui::Checkbox("fit text to box", &n->fitToBox);
         if (n->fitToBox)
            ImGui::TextDisabled("size is a maximum - fitted to %.0f pt", n->FittedSize());
         ModSlider("line spacing", &n->lineSpacing, 0.5f, 2.5f);
      }
      ModSlider("outline", &n->outlineWidth, 0.0f, 20.0f);
      if (n->outlineWidth > 0.0f)
      {
         ColorSwatch("outline colour", n->outlineColor, n);
         ImGui::Checkbox("outline only", &n->outlineOnly);
      }
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
   }

   void DrawVideoParams(VideoSourceNode* n)
   {
      if (ImGui::Button("Choose video...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      if (!n->LastError().empty())
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
         ImGui::PopTextWrapPos();
      }
      else if (!n->LoadedPath().empty())
      {
         std::string file = n->LoadedPath();
         size_t slash = file.find_last_of('/');
         if (slash != std::string::npos)
            file = file.substr(slash + 1);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", file.c_str());
         ImGui::PopTextWrapPos();
         ImGui::TextDisabled("%dx%d  %.1fs / %.1fs", n->GetOutputWidth(), n->GetOutputHeight(),
                             n->Position(), n->Duration());
      }
      ImGui::Checkbox("loop", &n->loop);
      ModSlider("speed", &n->speed, -2.0f, 4.0f);
   }

   void DrawFitParams(FitNode* n)
   {
      DropdownButton("mode", FitNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ImGui::Checkbox("match input res", &n->matchInput);
      if (!n->matchInput)
      {
         ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
         ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      }
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);
   }

   void DrawLFOParams(LFONode* n)
   {
      DropdownButton("shape", LFONode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("phase", &n->phase, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawRandomParams(RandomNode* n)
   {
      ModSlider("rate (beats)", &n->rateBeats, 0.05f, 32.0f);
      ModSlider("smooth", &n->smooth, 0.0f, 1.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawPatternParams(PatternNode* n)
   {
      ImGui::TextDisabled("8 steps, looped:");
      for (int i = 0; i < PatternNode::kSteps; i++)
      {
         char label[16];
         snprintf(label, sizeof(label), "%d", i + 1);
         const bool active = (i == n->CurrentStep()) && i < n->length;
         if (active)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.24f, 0.08f, 1.0f));
         ModSlider(label, &n->steps[i], 0.0f, 1.0f);
         if (active)
            ImGui::PopStyleColor();
      }
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("length", &n->length, 1, PatternNode::kSteps);
      ModSlider("beats / step", &n->stepBeats, 0.05f, 8.0f);
      ImGui::Checkbox("glide between steps", &n->smoothSteps);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
   }

   void DrawMathParams(MathNode* n)
   {
      DropdownButton("operation", MathNode::OpNames(), n->op, [n](int i) { n->op = i; });
      if (n->inputA == nullptr)
         ModSlider("A (no cable)", &n->constantA, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("A: patched");
      if (n->inputB == nullptr)
         ModSlider("B (no cable)", &n->constantB, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("B: patched");
      ModSlider("gain", &n->gain, -4.0f, 4.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ImGui::Checkbox("clamp to 0..1", &n->clampOutput);
   }

   void DrawNoiseParams(NoiseNode* n)
   {
      DropdownButton("type", NoiseNode::TypeNames(), n->noiseType, [n](int i) { n->noiseType = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("scale", &n->scale, 0.5f, 60.0f);
      ModSlider("octaves", &n->octaves, 1.0f, 8.0f, "%.0f");
      ModSlider("lacunarity", &n->lacunarity, 1.0f, 4.0f);
      ModSlider("gain", &n->gain, 0.1f, 0.9f);
      ModSlider("warp", &n->warp, 0.0f, 2.0f);
      ModSlider("speed", &n->speed, -2.0f, 2.0f);
      ModSlider("contrast", &n->contrast, 0.1f, 4.0f);
      ModSlider("brightness", &n->brightness, -0.5f, 0.5f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
      ImGui::Checkbox("rgb noise", &n->colorNoise);
      if (!n->colorNoise)
      {
         ColorSwatch("low", n->lowColor, n);
         ColorSwatch("high", n->highColor, n);
      }
   }

   void DrawSwitcherParams(SwitcherNode* n)
   {
      DropdownButton("unit", SwitcherNode::UnitNames(), n->unit, [n](int i) { n->unit = i; });
      ModSlider("every", &n->interval, 0.05f, 32.0f);
      ModSlider("crossfade", &n->crossfade, 0.0f, 0.99f);
      ImGui::Checkbox("manual", &n->manual);
      if (n->manual)
      {
         ImGui::SetNextItemWidth(kParamWidth);
         ImGui::SliderInt("slot", &n->manualSlot, 0, SwitcherNode::kSlots - 1);
      }
      else
      {
         ImGui::TextDisabled("showing input %c", 'A' + n->ActiveSlot());
      }
   }

   // 2D control surface. Dragging the orb sweeps the mutation weights; the
   // recorded path is drawn behind it so a loop is visible while it plays.
   void DrawFxPad(ResynthNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##fxpad", ImVec2(size, size));
      const bool active = ImGui::IsItemActive();

      if (active)
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);

      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y),
                     IM_COL32(48, 50, 62, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f),
                     IM_COL32(48, 50, 62, 255));
      }

      const std::vector<ResynthNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, IM_COL32(120, 200, 255, 170), 1.4f);
      }

      // Corner labels: the pad blends between these four named effects, so it is
      // obvious what is being swept rather than four anonymous weights.
      const ImU32 labelCol = IM_COL32(150, 156, 180, 255);
      const char* bl = n->CornerLabel(0);
      const char* brName = n->CornerLabel(1);
      const char* tl = n->CornerLabel(2);
      const char* trName = n->CornerLabel(3);
      dl->AddText(ImVec2(origin.x + 5, origin.y + 4), labelCol, tl);
      ImVec2 trSize = ImGui::CalcTextSize(trName);
      dl->AddText(ImVec2(br.x - trSize.x - 5, origin.y + 4), labelCol, trName);
      ImVec2 blSize = ImGui::CalcTextSize(bl);
      dl->AddText(ImVec2(origin.x + 5, br.y - blSize.y - 4), labelCol, bl);
      ImVec2 brSize = ImGui::CalcTextSize(brName);
      dl->AddText(ImVec2(br.x - brSize.x - 5, br.y - brSize.y - 4), labelCol, brName);

      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? IM_COL32(120, 235, 150, 255)
                                            : IM_COL32(255, 190, 90, 255);
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
   }

   void DrawResynthParams(ResynthNode* n)
   {
      ImGui::TextDisabled("FX pad - drag the orb");
      DrawFxPad(n);

      if (n->IsRecordingPath())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else
      {
         if (ImGui::Button("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else
      {
         if (ImGui::Button("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
            n->PlayPath();
      }
      ImGui::Checkbox("loop path", &n->loopPath);
      ImGui::SameLine();
      if (ImGui::SmallButton("clear"))
         n->ClearPath();

      NodeSeparator();
      DropdownButton("mode", ResynthNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      ModSlider("chaos", &n->chaos, 0.0f, 1.0f);
      ModSlider("mutation", &n->mutation, 0.0f, 1.0f);
      ModSlider("feedback", &n->feedback, 0.0f, 1.0f);
      ModSlider("source pull", &n->sourcePull, 0.0f, 1.0f);

      NodeSeparator();
      ImGui::TextDisabled("generation %d", n->Generation());
      if (ImGui::Button("Iterate", ImVec2(kPreviewSize * 0.48f, 0)))
         n->StepOnce();
      ImGui::SameLine();
      if (ImGui::Button("Reset", ImVec2(kPreviewSize * 0.48f, 0)))
         n->Reset();
      if (ImGui::Button("Randomise FX", ImVec2(kPreviewSize, 0)))
         n->Randomise();

      if (ImGui::TreeNode("pad corners"))
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
         ImGui::TreePop();
      }
      ImGui::Checkbox("auto iterate", &n->autoIterate);
      if (n->autoIterate)
         ModSlider("steps / beat", &n->stepsPerBeat, 0.05f, 16.0f);
      ModSlider("seed", &n->seed, 0.0f, 100.0f);
   }

   void DrawMacroKnobParams(MacroKnobNode* n)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "%s", n->label.c_str());
      ImGui::SetNextItemWidth(kParamWidth);
      if (ImGui::InputText("name", buf, sizeof(buf)))
         n->label = buf;

      ImGui::SetNextItemWidth(kPreviewSize);
      ImGui::SliderFloat("##macro", &n->value, 0.0f, 1.0f, "%.3f");
      ImGui::TextDisabled("patch 'out' into as many params as you like");
      ModSlider("curve", &n->curve, 0.2f, 4.0f);
      ImGui::Checkbox("invert", &n->invert);
   }

   void DrawMacroXYParams(MacroXYNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##macroxy", ImVec2(size, size));
      if (ImGui::IsItemActive())
      {
         ImVec2 m = ImGui::GetIO().MousePos;
         n->padX = std::min(1.0f, std::max(0.0f, (m.x - origin.x) / size));
         n->padY = std::min(1.0f, std::max(0.0f, 1.0f - (m.y - origin.y) / size));
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), IM_COL32(48, 50, 62, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), IM_COL32(48, 50, 62, 255));
      }
      const std::vector<MacroXYNode::PadPoint>& path = n->Path();
      for (size_t i = 1; i < path.size(); i++)
      {
         ImVec2 a(origin.x + path[i - 1].x * size, origin.y + (1.0f - path[i - 1].y) * size);
         ImVec2 b(origin.x + path[i].x * size, origin.y + (1.0f - path[i].y) * size);
         dl->AddLine(a, b, IM_COL32(120, 200, 255, 170), 1.4f);
      }
      ImVec2 orb(origin.x + n->padX * size, origin.y + (1.0f - n->padY) * size);
      ImU32 orbColor = n->IsRecordingPath() ? IM_COL32(255, 90, 90, 255)
                     : n->IsPlayingPath()   ? IM_COL32(120, 235, 150, 255)
                                            : IM_COL32(255, 190, 90, 255);
      dl->AddCircleFilled(orb, 9.0f, orbColor);
      dl->AddCircle(orb, 9.0f, IM_COL32(20, 20, 28, 255), 0, 2.0f);
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);

      ImGui::TextDisabled("x %.3f   y %.3f", n->padX, n->padY);

      if (n->IsRecordingPath())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button("Rec path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingPath())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPath();
      }
      else if (ImGui::Button("Play path", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayPath();
      }
      ImGui::Checkbox("loop", &n->loopPath);
      ImGui::SameLine();
      if (ImGui::SmallButton("clear"))
         n->ClearPath();
      ModSlider("speed", &n->speed, 0.05f, 4.0f);
   }

   // Interactive tone curve. Drag points, click empty space to add one,
   // right-click a point to remove it.
   void DrawCurveEditor(CurvesNode* n)
   {
      const float size = kPreviewSize;
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##curve", ImVec2(size, size));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();

      auto toScreen = [&](float x, float y) {
         return ImVec2(origin.x + x * size, origin.y + (1.0f - y) * size);
      };
      auto toCurve = [&](ImVec2 p) {
         return ImVec2(std::min(1.0f, std::max(0.0f, (p.x - origin.x) / size)),
                       std::min(1.0f, std::max(0.0f, 1.0f - (p.y - origin.y) / size)));
      };

      std::vector<CurvesNode::Point>& pts = n->Points(n->activeChannel);

      static int sDragIndex = -1;
      static CurvesNode* sDragNode = nullptr;

      const ImVec2 mouse = ImGui::GetIO().MousePos;
      int nearest = -1;
      float nearestDist = 12.0f;
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         float d = std::sqrt((sp.x - mouse.x) * (sp.x - mouse.x) + (sp.y - mouse.y) * (sp.y - mouse.y));
         if (d < nearestDist)
         {
            nearestDist = d;
            nearest = i;
         }
      }

      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         if (nearest >= 0)
         {
            sDragIndex = nearest;
            sDragNode = n;
         }
         else
         {
            ImVec2 c = toCurve(mouse);
            sDragIndex = n->AddPoint(n->activeChannel, c.x, c.y);
            sDragNode = n;
         }
      }
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && nearest >= 0)
         n->RemovePoint(n->activeChannel, nearest);

      if (active && sDragNode == n && sDragIndex >= 0)
      {
         ImVec2 c = toCurve(mouse);
         n->MovePoint(n->activeChannel, sDragIndex, c.x, c.y);
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         sDragIndex = -1;
         sDragNode = nullptr;
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 br(origin.x + size, origin.y + size);
      dl->AddRectFilled(origin, br, IM_COL32(16, 16, 22, 255), 4.0f);
      for (int i = 1; i < 4; i++)
      {
         float f = (float)i / 4.0f;
         dl->AddLine(ImVec2(origin.x + size * f, origin.y), ImVec2(origin.x + size * f, br.y), IM_COL32(44, 46, 58, 255));
         dl->AddLine(ImVec2(origin.x, origin.y + size * f), ImVec2(br.x, origin.y + size * f), IM_COL32(44, 46, 58, 255));
      }
      dl->AddLine(origin, ImVec2(br.x, br.y), IM_COL32(58, 60, 74, 255)); // identity reference

      ImU32 lineCol = IM_COL32(230, 235, 250, 255);
      if (n->activeChannel == CurvesNode::kRed)   lineCol = IM_COL32(255, 110, 110, 255);
      if (n->activeChannel == CurvesNode::kGreen) lineCol = IM_COL32(120, 230, 130, 255);
      if (n->activeChannel == CurvesNode::kBlue)  lineCol = IM_COL32(120, 170, 255, 255);

      const int kSegments = 64;
      for (int i = 1; i <= kSegments; i++)
      {
         float x0 = (float)(i - 1) / kSegments;
         float x1 = (float)i / kSegments;
         dl->AddLine(toScreen(x0, n->Evaluate(n->activeChannel, x0)),
                     toScreen(x1, n->Evaluate(n->activeChannel, x1)), lineCol, 1.8f);
      }
      for (int i = 0; i < (int)pts.size(); i++)
      {
         ImVec2 sp = toScreen(pts[i].x, pts[i].y);
         dl->AddCircleFilled(sp, i == nearest ? 6.0f : 4.5f, lineCol);
         dl->AddCircle(sp, i == nearest ? 6.0f : 4.5f, IM_COL32(18, 18, 26, 255), 0, 1.5f);
      }
      dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
   }

   void DrawCurvesParams(CurvesNode* n)
   {
      DropdownButton("channel", CurvesNode::ChannelNames(), n->activeChannel,
                     [n](int i) { n->activeChannel = i; });
      DrawCurveEditor(n);
      ImGui::TextDisabled("drag points, click to add, right-click to remove");
      if (ImGui::Button("Reset channel", ImVec2(kPreviewSize, 0)))
         n->ResetChannel(n->activeChannel);
      ModSlider("mix", &n->mix, 0.0f, 1.0f);
   }

   void DrawRemoveBgParams(RemoveBgNode* n)
   {
      DropdownButton("detect", RemoveBgNode::ModeNames(), n->mode, [n](int i) { n->mode = i; });
      DropdownButton("output", RemoveBgNode::OutputModeNames(), n->outputMode,
                     [n](int i) { n->outputMode = i; });

      if (ImGui::Button("Remove Background", ImVec2(kPreviewSize, 0)))
         n->RequestMask();

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      ModSlider("feather", &n->feather, 0.0f, 4.0f);
      ModSlider("threshold", &n->threshold, 0.0f, 1.0f);
      ModSlider("edge contrast", &n->contrast, 0.5f, 8.0f);
      ColorSwatch("bg", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      ImGui::Checkbox("auto refresh (video)", &n->autoRefresh);
      if (n->autoRefresh)
      {
         ModSlider("every beats", &n->refreshBeats, 0.1f, 8.0f);
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("segmentation is expensive - it runs on this interval, not every frame");
         ImGui::PopTextWrapPos();
      }
   }

   void DrawFeedbackParams(FeedbackNode*)
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("Outputs the previous frame. Patch a downstream node's "
                          "output back through this to build a loop without the "
                          "graph recursing.");
      ImGui::PopTextWrapPos();
   }

   void DrawTrailsParams(TrailsNode* n)
   {
      static const std::vector<std::string> kBlends = { "Max", "Add", "Screen" };
      DropdownButton("blend", kBlends, n->blendMode, [n](int i) { n->blendMode = i; });
      ModSlider("decay", &n->decay, 0.5f, 0.999f);
      ModSlider("zoom", &n->zoom, 0.9f, 1.1f);
      ModSlider("rotate", &n->rotate, -0.1f, 0.1f);
      ModSlider("drift x", &n->driftX, -0.02f, 0.02f);
      ModSlider("drift y", &n->driftY, -0.02f, 0.02f);
      ModSlider("hue shift", &n->hueShift, -0.05f, 0.05f);
      if (ImGui::Button("Clear", ImVec2(kPreviewSize, 0)))
         n->Clear();
   }

   void DrawReactionDiffusionParams(ReactionDiffusionNode* n)
   {
      DropdownButton("preset", ReactionDiffusionNode::PresetNames(), n->preset,
                     [n](int i) { n->ApplyPreset(i); });
      ModSlider("feed", &n->feed, 0.01f, 0.09f, "%.4f");
      ModSlider("kill", &n->kill, 0.03f, 0.08f, "%.4f");
      ModSlider("diffuse A", &n->diffuseA, 0.2f, 1.5f);
      ModSlider("diffuse B", &n->diffuseB, 0.1f, 1.0f);
      ModSlider("steps / frame", &n->stepsPerFrame, 1.0f, 32.0f, "%.0f");
      ImGui::TextDisabled("with an input connected:");
      ModSlider("source influence", &n->sourceInfluence, 0.0f, 1.0f);
      ModSlider("width", &n->width, 64.0f, 2048.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 2048.0f, "%.0f");
      ColorSwatch("low", n->lowColor, n);
      ColorSwatch("high", n->highColor, n);
      if (ImGui::Button("Reseed", ImVec2(kPreviewSize, 0)))
         n->Reseed();
   }

   void DrawRampParams(RampNode* n)
   {
      DropdownButton("type", RampNode::TypeNames(), n->type, [n](int i) { n->type = i; });
      DropdownButton("repeat", RampNode::RepeatNames(), n->repeat, [n](int i) { n->repeat = i; });
      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModSlider("angle", &n->angle, -3.1416f, 3.1416f);
      ModSlider("center x", &n->centerX, -0.5f, 1.5f);
      ModSlider("center y", &n->centerY, -0.5f, 1.5f);
      ModSlider("scale", &n->scale, 0.05f, 8.0f);
      ModSlider("offset", &n->offset, -1.0f, 1.0f);
      ModSlider("gamma", &n->gamma, 0.1f, 4.0f);
      ModSlider("dither", &n->dither, 0.0f, 1.0f);

      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("stops", &n->stopCount, 2, RampNode::kStops);
      for (int i = 0; i < n->stopCount; i++)
      {
         ImGui::PushID(i);
         char label[24];
         snprintf(label, sizeof(label), "stop %d", i + 1);
         ColorSwatch(label, n->stopColor[i], n);
         ModSlider("at", &n->stopPos[i], 0.0f, 1.0f);
         ImGui::PopID();
      }
   }

   void DrawImageAnalyzeParams(ImageAnalyzeNode* n)
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      ImGui::TextDisabled("Turns an image into control values. Patch any output "
                          "into any slider - a video can drive a blur.");
      ImGui::PopTextWrapPos();
      for (int i = 0; i < ImageAnalyzeNode::kOutputCount; i++)
      {
         const float v = n->Value(i);
         ImGui::Text("%-9s", n->OutputLabel(i));
         ImGui::SameLine();
         ImGui::ProgressBar(v, ImVec2(kPreviewSize * 0.55f, 0), "");
      }
      ModSlider("gain", &n->gain, 0.1f, 8.0f);
      ModSlider("smoothing", &n->smoothing, 0.0f, 0.95f);
      ModSlider("samples / sec", &n->sampleRate, 1.0f, 60.0f, "%.0f");
      ImGui::SetNextItemWidth(kParamWidth);
      ImGui::SliderInt("sample res", &n->sampleSize, 8, 256);
      ImGui::TextDisabled("readback is rate-limited; it stalls the GPU");
   }

   void DrawAudioFileParams(AudioFileNode* n)
   {
      if (ImGui::Button("Choose audio...", ImVec2(kPreviewSize, 0)))
         n->OpenViaDialog();

      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
      if (!n->FileName().empty())
         ImGui::TextDisabled("%s", n->FileName().c_str());
      ImGui::TextDisabled("%s", n->Status().c_str());
      ImGui::PopTextWrapPos();

      if (n->IsLoaded())
      {
         const double dur = std::max(0.001, n->Duration());
         ImGui::ProgressBar((float)(n->Position() / dur), ImVec2(kPreviewSize, 0));
         ImGui::TextDisabled("%.1f / %.1f s", n->Position(), dur);

         if (n->IsPlaying())
         {
            if (ImGui::Button("Pause", ImVec2(kPreviewSize * 0.48f, 0)))
               n->Pause();
         }
         else if (ImGui::Button("Play", ImVec2(kPreviewSize * 0.48f, 0)))
         {
            n->Play();
         }
         ImGui::SameLine();
         if (ImGui::Button("Restart", ImVec2(kPreviewSize * 0.48f, 0)))
            n->Restart();

         ImGui::Checkbox("follow transport", &n->followTransport);
         ImGui::Checkbox("loop", &n->loop);
         ImGui::Checkbox("audible", &n->monitor);
         if (!n->monitor)
            ImGui::TextDisabled("silent, but still analysed");
         ModSlider("volume", &n->volume, 0.0f, 1.0f);
         ModSlider("gain", &n->gain, 0.1f, 16.0f);

         const Platform::AudioLevels& lv = n->Levels();
         ImGui::Text("level "); ImGui::SameLine();
         ImGui::ProgressBar(std::min(1.0f, lv.rms * n->gain), ImVec2(kPreviewSize * 0.6f, 0), "");
      }
      ImGui::TextDisabled("patch 'out' into Audio Analyze");
   }

   void DrawAudioAnalyzeParams(AudioAnalyzeNode* n)
   {
      if (n->fileSource != nullptr)
      {
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "source: Audio File");
         ImGui::PopTextWrapPos();
      }
      else
      {
         if (Platform::AudioIsRunning())
         {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Stop listening", ImVec2(kPreviewSize, 0)))
               n->Stop();
            ImGui::PopStyleColor();
         }
         else if (ImGui::Button("Start listening", ImVec2(kPreviewSize, 0)))
         {
            n->Start();
         }
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
         ImGui::TextDisabled("%s", n->Status().c_str());
         ImGui::PopTextWrapPos();
      }

      // spectrum, so it is obvious whether audio is actually arriving
      const Platform::AudioLevels& levels = n->Levels();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 60.0f;
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), IM_COL32(16, 16, 22, 255), 3.0f);
      const float bw = kPreviewSize / (float)Platform::kAudioBands;
      for (int i = 0; i < Platform::kAudioBands; i++)
      {
         const float v = std::min(1.0f, levels.bands[i] * n->gain);
         dl->AddRectFilled(ImVec2(origin.x + i * bw + 1, origin.y + h - v * h),
                           ImVec2(origin.x + (i + 1) * bw - 1, origin.y + h),
                           IM_COL32(120, 200, 255, 235));
      }
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h), IM_COL32(70, 74, 90, 255), 3.0f);

      for (int i = 0; i < 5; i++)
      {
         ImGui::Text("%-6s", n->OutputLabel(i));
         ImGui::SameLine();
         ImGui::ProgressBar(n->Value(i), ImVec2(kPreviewSize * 0.6f, 0), "");
      }
      ModSlider("gain", &n->gain, 0.1f, 16.0f);
      ModSlider("attack", &n->attack, 0.02f, 1.0f);
      ModSlider("release", &n->release, 0.005f, 1.0f);
      ModSlider("onset hold", &n->onsetHold, 0.02f, 1.0f);
   }

   void DrawGeometryParams(GeometryNode* n)
   {
      DropdownButton("shape", GeometryNode::ShapeNames(), n->shape, [n](int i) { n->shape = i; });
      ImGui::TextDisabled("%zu triangles", n->TriangleCount());

      NodeSeparator("form");
      ModSliderInt("detail", &n->detail, 2, 96);
      ModSliderInt("sides", &n->sides, 3, 64);
      if (n->shape == 4 || n->shape == 7)
         ModSlider("tube", &n->tubeRadius, 0.02f, 0.95f);
      if (n->shape == 7)
      {
         ModSliderInt("knot p", &n->knotP, 1, 8);
         ModSliderInt("knot q", &n->knotQ, 1, 8);
      }

      NodeSeparator("transform");
      ModSlider("pos x", &n->posX, -3.0f, 3.0f);
      ModSlider("pos y", &n->posY, -3.0f, 3.0f);
      ModSlider("pos z", &n->posZ, -3.0f, 3.0f);
      ModSlider("rot x", &n->rotX, -3.1416f, 3.1416f);
      ModSlider("rot y", &n->rotY, -3.1416f, 3.1416f);
      ModSlider("rot z", &n->rotZ, -3.1416f, 3.1416f);
      ModSlider("scale", &n->uniformScale, 0.05f, 4.0f);
      ModSlider("scale x", &n->scaleX, 0.05f, 4.0f);
      ModSlider("scale y", &n->scaleY, 0.05f, 4.0f);
      ModSlider("scale z", &n->scaleZ, 0.05f, 4.0f);
      ModSlider("spin / beat", &n->spinY, -3.1416f, 3.1416f);

      NodeSeparator("material");
      DropdownButton("shading", GeometryNode::ShadingNames(), n->shading, [n](int i) { n->shading = i; });
      ColorSwatch("colour", n->color, n);
      ModSlider("metallic", &n->metallic, 0.0f, 1.0f);
      ModSlider("roughness", &n->roughness, 0.02f, 1.0f);
      ModSlider("opacity", &n->opacity, 0.0f, 1.0f);
      ImGui::TextDisabled("patch an image into the input to texture it");
      ImGui::TextDisabled("patch 'out' into Render 3D");
   }

   void DrawRender3DParams(Render3DNode* n)
   {
      int connected = 0;
      for (int i = 0; i < Render3DNode::kSlots; i++)
         if (n->geometry[i] != nullptr)
            connected++;
      ImGui::TextDisabled("%d geometry input%s connected", connected, connected == 1 ? "" : "s");

      NodeSeparator("output");
      ModSlider("width", &n->width, 64.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 64.0f, 4096.0f, "%.0f");
      ColorSwatch("background", n->bgColor, n);
      ModSlider("bg opacity", &n->bgOpacity, 0.0f, 1.0f);

      NodeSeparator("camera");
      DropdownButton("projection", Render3DNode::ProjectionNames(), n->projection,
                     [n](int i) { n->projection = i; });
      if (n->projection == 0)
         ModSlider("fov", &n->fov, 10.0f, 120.0f);
      else
         ModSlider("ortho height", &n->orthoHeight, 0.2f, 8.0f);
      ModSlider("distance", &n->camDistance, 0.3f, 20.0f);
      ModSlider("orbit", &n->camAzimuth, -3.1416f, 3.1416f);
      ModSlider("elevation", &n->camElevation, -1.5f, 1.5f);
      ModSlider("target x", &n->targetX, -3.0f, 3.0f);
      ModSlider("target y", &n->targetY, -3.0f, 3.0f);
      ModSlider("target z", &n->targetZ, -3.0f, 3.0f);

      NodeSeparator("light");
      ModSlider("light orbit", &n->lightAzimuth, -3.1416f, 3.1416f);
      ModSlider("light height", &n->lightElevation, -1.5f, 1.5f);
      ColorSwatch("light", n->lightColor, n);
      ModSlider("intensity", &n->lightIntensity, 0.0f, 4.0f);
      ColorSwatch("ambient", n->ambientColor, n);
      ModSlider("rim", &n->rimIntensity, 0.0f, 2.0f);

      NodeSeparator("raster");
      ImGui::Checkbox("depth test", &n->depthTest);
      ImGui::Checkbox("cull backfaces", &n->backfaceCull);
   }

   void DrawBlendParams(BlendNode* n)
   {
      DropdownButton("mode", BlendNode::ModeNames(), n->ModeIndex(),
                     [n](int i) { n->ModeIndex() = i; });
      ModSlider("opacity", &n->Mix(), 0.0f, 1.0f);
   }

   void DrawLayerStackParams(LayerStackNode* n)
   {
      // Layers composite bottom-up: A is the base, D sits on top. Grab a layer's
      // header strip and drag vertically to reorder - the whole layer moves,
      // cable, blend mode and opacity together.
      ImGui::TextDisabled("A is the base, D is on top - drag a header to reorder");

      static LayerStackNode* sDragNode = nullptr;
      static int sDragSlot = -1;
      static float sDragAccum = 0.0f;
      const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

      for (int slot = 0; slot < LayerStackNode::kSlots; slot++)
      {
         ImGui::PushID(slot);

         const bool dragging = (sDragNode == n && sDragSlot == slot);
         ImVec2 headerPos = ImGui::GetCursorScreenPos();
         ImGui::InvisibleButton("##grip", ImVec2(kPreviewSize, rowHeight));
         if (ImGui::IsItemActive() && sDragNode == nullptr)
         {
            sDragNode = n;
            sDragSlot = slot;
            sDragAccum = 0.0f;
         }

         ImDrawList* dl = ImGui::GetWindowDrawList();
         if (dragging || ImGui::IsItemHovered())
         {
            dl->AddRectFilled(headerPos,
                              ImVec2(headerPos.x + kPreviewSize, headerPos.y + rowHeight),
                              dragging ? IM_COL32(70, 90, 130, 200) : IM_COL32(50, 54, 68, 160), 3.0f);
         }
         // grip dots, so the header reads as draggable
         for (int d = 0; d < 3; d++)
         {
            dl->AddCircleFilled(ImVec2(headerPos.x + 6, headerPos.y + 5 + d * 4.0f), 1.3f,
                                IM_COL32(140, 146, 168, 255));
            dl->AddCircleFilled(ImVec2(headerPos.x + 11, headerPos.y + 5 + d * 4.0f), 1.3f,
                                IM_COL32(140, 146, 168, 255));
         }
         // Name the layer after whatever is feeding it - far more useful than
         // "layer C" once a stack has four things in it.
         char title[96];
         const INode* source = n->Input(slot).GetSource();
         if (source != nullptr)
         {
            const char* sourceName = "?";
            for (const GraphNode& other : gNodes)
            {
               if (other.node.get() == source)
                  sourceName = other.typeName.c_str();
            }
            snprintf(title, sizeof(title), "%c  %s", 'A' + slot, sourceName);
         }
         else
         {
            snprintf(title, sizeof(title), "%c  (empty)", 'A' + slot);
         }
         dl->AddText(ImVec2(headerPos.x + 20, headerPos.y + 2),
                     source ? IM_COL32(190, 196, 215, 255) : IM_COL32(120, 124, 142, 255), title);

         char modeLabel[32];
         snprintf(modeLabel, sizeof(modeLabel), "mode##%d", slot);
         DropdownButton(modeLabel, BlendModes::Names(), n->modes[slot],
                        [n, slot](int i) { n->modes[slot] = i; });
         ModSlider("opacity", &n->opacities[slot], 0.0f, 1.0f);
         ImGui::PopID();
      }

      // Resolve the drag once per frame: accumulate vertical movement and swap a
      // slot at a time so the layer follows the cursor.
      if (sDragNode == n && sDragSlot >= 0)
      {
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
         {
            sDragNode = nullptr;
            sDragSlot = -1;
         }
         else
         {
            sDragAccum += ImGui::GetIO().MouseDelta.y;
            const float threshold = rowHeight * 3.0f; // one full layer block
            while (sDragAccum > threshold && sDragSlot < LayerStackNode::kSlots - 1)
            {
               n->SwapLayers(sDragSlot, sDragSlot + 1);
               sDragSlot++;
               sDragAccum -= threshold;
            }
            while (sDragAccum < -threshold && sDragSlot > 0)
            {
               n->SwapLayers(sDragSlot, sDragSlot - 1);
               sDragSlot--;
               sDragAccum += threshold;
            }
         }
      }
   }

   void DrawFilterParams(FilterNode* n)
   {
      const FilterDef& def = n->Def();
      for (size_t i = 0; i < def.params.size(); i++)
      {
         const FilterParamDef& p = def.params[i];
         ImGui::PushID((int)i);
         if (p.type == FilterParamDef::Type::Color)
         {
            ColorSwatch(p.label.c_str(), n->ParamPtr(i), n);
         }
         else if (p.type == FilterParamDef::Type::Enum)
         {
            float* slot = n->ParamPtr(i);
            DropdownButton(p.label.c_str(), p.options, (int)(*slot + 0.5f),
                           [slot](int choice) { *slot = (float)choice; });
         }
         else
         {
            ModSlider(p.label.c_str(), n->ParamPtr(i), p.minVal, p.maxVal);
         }
         ImGui::PopID();
      }
      if (def.params.empty())
         ImGui::TextDisabled("(no parameters)");
   }

   void ExportPng(OutputNode* out, const std::string& path)
   {
      int w = out->GetOutputWidth();
      int h = out->GetOutputHeight();
      if (w <= 0 || h <= 0)
         return;

      std::vector<unsigned char> pixels(w * h * 4);
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out->GetOutputTexture(), 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glDeleteFramebuffers(1, &fbo);

      stbi_flip_vertically_on_write(1);
      stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
   }

   // Paintable preview: the Draw node turns its 1:1 preview into the canvas, so
   // you draw directly on the node rather than in a separate window.
   void DrawPaintablePreview(DrawNode* node)
   {
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##canvas", ImVec2(kPreviewSize, kPreviewSize));

      const int w = node->GetOutputWidth();
      const int h = node->GetOutputHeight();
      float dw = kPreviewSize, dh = kPreviewSize;
      if (w > 0 && h > 0)
      {
         const float scale = kPreviewSize / (float)std::max(w, h);
         dw = w * scale;
         dh = h * scale;
      }
      const ImVec2 tl(origin.x + (kPreviewSize - dw) * 0.5f, origin.y + (kPreviewSize - dh) * 0.5f);

      if (ImGui::IsItemActive())
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const float u = (m.x - tl.x) / std::max(1.0f, dw);
         const float v = 1.0f - (m.y - tl.y) / std::max(1.0f, dh); // texture space is bottom-up
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            node->BeginStroke(u, v);
         else
            node->ContinueStroke(u, v);
      }
      else if (ImGui::IsItemDeactivated())
      {
         node->EndStroke();
      }

      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                        IM_COL32(18, 18, 24, 255), 4.0f);
      // checkerboard, so transparent areas of the canvas are obvious
      for (int y = 0; y < 8; y++)
      {
         for (int x = 0; x < 8; x++)
         {
            if ((x + y) % 2)
               continue;
            const float c = kPreviewSize / 8.0f;
            dl->AddRectFilled(ImVec2(origin.x + x * c, origin.y + y * c),
                              ImVec2(origin.x + (x + 1) * c, origin.y + (y + 1) * c),
                              IM_COL32(30, 30, 38, 255));
         }
      }
      if (node->GetOutputTexture() != 0)
         dl->AddImage((ImTextureID)(intptr_t)node->GetOutputTexture(), tl,
                      ImVec2(tl.x + dw, tl.y + dh), ImVec2(0, 1), ImVec2(1, 0));
      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                  IM_COL32(90, 130, 190, 255), 4.0f, 0, 2.0f);
   }

   void DrawDrawParams(DrawNode* n)
   {
      DropdownButton("brush", DrawNode::BrushNames(), n->brush, [n](int i) { n->brush = i; });
      ModSlider("size", &n->brushSize, 0.002f, 0.5f);
      ModSlider("opacity", &n->opacity, 0.02f, 1.0f);
      ModSlider("hardness", &n->hardness, 0.0f, 0.98f);
      ModSlider("spacing", &n->spacing, 0.02f, 1.0f);
      ModSlider("jitter", &n->jitter, 0.0f, 2.0f);
      ColorSwatch("colour", n->color, n);
      ImGui::Checkbox("eraser", &n->eraser);
      if (ImGui::Button("Clear canvas", ImVec2(kPreviewSize, 0)))
         n->ClearCanvas();

      NodeSeparator("animation");
      if (n->IsRecordingStrokes())
      {
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
         if (ImGui::Button("Stop rec", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopRecording();
         ImGui::PopStyleColor();
      }
      else if (ImGui::Button("Rec strokes", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->StartRecording();
      }
      ImGui::SameLine();
      if (n->IsPlayingBack())
      {
         if (ImGui::Button("Stop", ImVec2(kPreviewSize * 0.48f, 0)))
            n->StopPlayback();
      }
      else if (ImGui::Button("Replay", ImVec2(kPreviewSize * 0.48f, 0)))
      {
         n->PlayRecording();
      }

      if (n->RecordedStamps() > 0)
      {
         ImGui::TextDisabled("%zu marks over %.1f beats", n->RecordedStamps(), n->RecordedLength());
         if (n->IsPlayingBack())
            ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.6f, 1.0f), "playhead %.1f", n->PlayheadBeats());
      }
      else
      {
         ImGui::TextDisabled("record, then draw - replay redraws it in time");
      }
      ImGui::Checkbox("loop replay", &n->loopPlayback);
      ModSlider("replay speed", &n->playSpeed, 0.1f, 4.0f);
      if (ImGui::SmallButton("clear recording"))
         n->ClearRecording();
      ImGui::TextDisabled("(canvas size follows the input when one is patched in)");
      ModSlider("canvas w", &n->canvasWidth, 64.0f, 4096.0f, "%.0f");
      ModSlider("canvas h", &n->canvasHeight, 64.0f, 4096.0f, "%.0f");
   }

   // Square, letterboxed preview so non-square sources still read 1:1.
   void DrawPreview(INode* node)
   {
      unsigned int tex = node->GetOutputTexture();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      if (tex != 0 && node->GetOutputWidth() > 0)
      {
         float w = (float)node->GetOutputWidth();
         float h = (float)node->GetOutputHeight();
         float scale = kPreviewSize / std::max(w, h);
         float dw = w * scale;
         float dh = h * scale;
         ImVec2 tl(origin.x + (kPreviewSize - dw) * 0.5f, origin.y + (kPreviewSize - dh) * 0.5f);
         dl->AddImage((ImTextureID)(intptr_t)tex, tl, ImVec2(tl.x + dw, tl.y + dh),
                      ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 10, origin.y + kPreviewSize * 0.5f - 8),
                     IM_COL32(120, 120, 135, 255), "no input");
      }

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + kPreviewSize),
                  IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize));
   }

   // Rolling history so a modulator reads like a scope rather than a number.
   std::map<int, std::vector<float>> gModHistory;

   void DrawModulatorMeter(IModulator* mod, int nodeIndex)
   {
      const float value = mod->Value01();
      std::vector<float>& history = gModHistory[nodeIndex];
      history.push_back(value);
      if (history.size() > 160)
         history.erase(history.begin());

      ImVec2 origin = ImGui::GetCursorScreenPos();
      const float h = 90.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                        IM_COL32(18, 18, 24, 255), 4.0f);

      for (size_t i = 1; i < history.size(); i++)
      {
         float x0 = origin.x + kPreviewSize * (float)(i - 1) / 160.0f;
         float x1 = origin.x + kPreviewSize * (float)i / 160.0f;
         float y0 = origin.y + h - history[i - 1] * h;
         float y1 = origin.y + h - history[i] * h;
         dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 190, 90, 255), 1.6f);
      }

      dl->AddRect(origin, ImVec2(origin.x + kPreviewSize, origin.y + h),
                  IM_COL32(70, 74, 90, 255), 4.0f);
      ImGui::Dummy(ImVec2(kPreviewSize, h));
      ImGui::Text("%.3f", value);
   }

   void DisconnectLinkById(int id)
   {
      const LinkInfo* link = FindLink(id);
      if (link == nullptr)
         return;

      const int dstPin = link->dstPin;
      GraphNode* dst = FindNodeByIndex(GraphNode::NodeIndexFromPin(dstPin));
      if (dst == nullptr)
         return;

      if (GraphNode::IsParamPin(dstPin))
      {
         Modulation::Instance().Unbind(dst->index, GraphNode::ParamIndexFromPin(dstPin));
      }
      else if (auto* render = dynamic_cast<Render3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Render3DNode::kSlots)
            render->geometry[slot] = nullptr;
      }
      else if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(dst->node.get()))
      {
         audio->fileSource = nullptr;
      }
      else if (auto* math = dynamic_cast<MathNode*>(dst->node.get()))
      {
         if (GraphNode::InputSlotFromPin(dstPin) == 0)
            math->inputA = nullptr;
         else
            math->inputB = nullptr;
      }
      else if (ImageCable* cable = CableFor(*dst, GraphNode::InputSlotFromPin(dstPin)))
      {
         cable->Disconnect();
      }
   }

   void DrawHelpWindow(bool* open)
   {
      ImGui::SetNextWindowSize(ImVec2(720, 620), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Infinite - help & module reference", open))
      {
         ImGui::End();
         return;
      }

      if (ImGui::CollapsingHeader("Getting started", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "Infinite is a node graph. Every node renders an image and passes it "
            "down a cable to the next one. A typical patch reads left to right:");
         ImGui::Bullet(); ImGui::TextWrapped("Source (image, video, shape, noise, formula) makes a picture.");
         ImGui::Bullet(); ImGui::TextWrapped("Effects and Color nodes change it.");
         ImGui::Bullet(); ImGui::TextWrapped("Compositing nodes combine several pictures into one.");
         ImGui::Bullet(); ImGui::TextWrapped("Output shows the result, exports a PNG, or records a video.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Nothing enforces that order - any output can feed any input, including "
            "back into effects for feedback-style chains.");
      }

      if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
      {
         if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
         {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("How");
            ImGui::TableHeadersRow();
            struct Row { const char* a; const char* b; };
            static const Row rows[] = {
               { "Add a node", "Right-click or double-click the canvas, then type to filter" },
               { "Connect", "Drag from a node's 'out' dot to another node's input dot" },
               { "Modulate a parameter", "Drag a modulator's 'out' onto the small dot beside any slider" },
               { "Type an exact value", "Double-click a slider" },
               { "Pan the canvas", "Drag empty canvas" },
               { "Rubber-band select", "Shift + drag" },
               { "Duplicate", "Cmd+C / Cmd+V, or Shift+D to duplicate in place" },
               { "Select several", "Shift + drag a box around them, or Shift-click (or Ctrl-click) each one to add it. Then move, duplicate or delete as a group." },
               { "Delete", "Select, then Delete or Backspace" },
               { "Zoom", "Scroll (speed is adjustable in the Menu)" },
               { "Play / pause everything", "Play button in the top bar" },
            };
            for (const Row& r : rows)
            {
               ImGui::TableNextRow();
               ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r.a);
               ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r.b);
            }
            ImGui::EndTable();
         }
      }

      if (ImGui::CollapsingHeader("Transport and modulation", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "The top bar holds a global clock: Play/Pause, Rewind and BPM. Everything "
            "time-based reads from it - modulators, video playback and animated shaders - "
            "so pausing freezes the whole patch and changing the tempo retimes all of it "
            "at once.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Modulator rates are given in beats, not seconds. A rate of 4 means one "
            "cycle every four beats, so it stays in time when you change the BPM.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Every slider has a small dot to its left. Patch a modulator into that dot "
            "and the slider turns amber and becomes read-only - the value is now being "
            "driven. Delete the cable to take manual control back.");
      }

      if (ImGui::CollapsingHeader("Using Feedback", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::TextWrapped(
            "A Feedback node outputs what its input produced on the PREVIOUS frame. "
            "That one-frame delay is the whole point: it lets you wire a cycle "
            "without the graph chasing its own tail forever.");
         ImGui::Spacing();
         ImGui::TextWrapped("Feedback on its own does nothing visible - it is a delay, not an effect. "
                            "It only earns its keep inside a loop. The classic patch:");
         ImGui::Indent();
         ImGui::Bullet(); ImGui::TextWrapped("Shape (or any source)  ->  Blend input A");
         ImGui::Bullet(); ImGui::TextWrapped("Feedback  ->  Blend input B");
         ImGui::Bullet(); ImGui::TextWrapped("Blend  ->  transform  (scale 1.02, small rotation)");
         ImGui::Bullet(); ImGui::TextWrapped("transform  ->  back into Feedback's input   <- this closes the loop");
         ImGui::Unindent();
         ImGui::Spacing();
         ImGui::TextWrapped(
            "Now every frame is the previous frame, nudged, with the source drawn "
            "over the top - which gives you infinite-zoom tunnels, echoes and "
            "growth. Set the Blend to Screen or Lighten and lower the opacity so "
            "the history fades rather than saturating.");
         ImGui::Spacing();
         ImGui::TextWrapped(
            "If you just want trails, use the Trails node instead - it is that same "
            "loop wrapped into one node, with decay, drift, zoom and rotation built "
            "in. Reaction-Diffusion is the other pre-wired feedback node: it needs "
            "no input at all and simulates a chemical system frame over frame.");
      }

      if (ImGui::CollapsingHeader("Module reference"))
      {
         struct Entry { const char* name; const char* text; };
         struct Group { const char* category; std::vector<Entry> entries; };
         static const std::vector<Group> groups = {
            { "Source", {
               { "Image Source", "Loads a still image. Opens the native file picker and decodes anything macOS can read - PNG, JPEG, TIFF, HEIC, RAW and more." },
               { "Video", "Plays a video file. Position follows the transport, so it pauses with everything else. Loop and speed (including reverse) are available." },
               { "Shape", "Ten vector primitives - circle, ellipse, rectangle, rounded rect, triangle, polygon, star, ring, cross, line - with fill, stroke, feather and background." },
               { "Noise", "Procedural noise: value, fBm, ridged, Voronoi, Worley edges and white. Domain warping, octaves and colour mapping included." },
               { "Draw", "Paint straight onto the node preview. Six procedural brushes, eraser, spacing and jitter. Patch an image in to paint over it. Strokes can be recorded and replayed as an animation." },
               { "Formula", "A live GLSL shader. Pick a preset or press 'Edit GLSL...' to write your own; four knobs (uA-uD) are exposed for modulation." },
            } },
            { "Text", {
               { "Text", "Renders text using any font installed on the system, with size, colour, tracking, alignment and position." },
            } },
            { "Effects", {
               { "Blur family", "Gaussian, box, motion (angle + distance) and radial (with a centre point)." },
               { "Bloom / Diffuse Glow", "Bloom isolates pixels above a threshold and blooms them outward. Diffuse Glow screens a blurred copy back over the image." },
               { "Sharpen", "Unsharp mask - blurs a copy and adds back the difference." },
               { "Distortion", "Twirl, pinch/punch, ripple, lens distortion (with chromatic aberration), displace and liquify. Position-dependent effects have Centre X/Y." },
               { "Glitch family", "Five kinds: the original combined glitch, RGB shift, scanlines, blocks, wave and datamosh." },
               { "Symmetry", "Symmetry (mirror about X, Y or both), Kaleidoscope (segment count, rotation, zoom) and Mirror Tile." },
               { "Stylise", "Halftone (mono or CMY-style colour), Sobel edge detection and Edge Outline." },
               { "Transform", "Translate, scale and rotate - filed under Effects rather than as a separate category." },
               { "Pixelate / Noise / Vignette", "Block pixelation, additive grain and a vignette with its own centre." },
            } },
            { "Color", {
               { "Basic", "Brightness/contrast, exposure, levels (black/white point + gamma), invert, posterize, threshold, vibrance." },
               { "Curves", "Shadow / midtone / highlight lift plus an S-curve control." },
               { "LUT", "Applies a lookup-table image patched into the second input." },
               { "Gradient Map", "Remaps luminance onto a two-colour gradient." },
               { "Channel Mixer", "Rebuilds each output channel from a weighted mix of the input channels." },
               { "HSL / Colour Balance / Black & White", "Hue, saturation and lightness; per-axis colour shifts; weighted greyscale." },
            } },
            { "Compositing", {
               { "Blend", "Two inputs and 31 blend modes - the full Normal / Multiply / Screen / Overlay / Hue / Saturation / Colour / Luminosity set, plus Erase." },
               { "Layer Stack", "Four inputs stacked bottom-up: A is the base, D sits on top. Each layer has its own blend mode and opacity, and dragging a layer header reorders the whole layer." },
               { "Switcher", "Cycles between its connected inputs every N beats or seconds, with an optional crossfade. Can be pinned to one input with 'manual'." },
               { "Fit", "Resamples an input to a chosen resolution. Fit letterboxes, Fill crops, Stretch ignores aspect, Native passes through. Use it to make differently-sized sources composite predictably." },
               { "Drop Shadow / Outer Glow / Colour Overlay", "Layer-effect style filters." },
            } },
            { "Modulators", {
               { "LFO", "Sine, triangle, saw up/down, square and sample-and-hold. Rate in beats, plus phase and an output range." },
               { "Random", "A new random value every N beats, with adjustable smoothing between steps. Deterministic, so rewinding replays the same sequence." },
               { "Pattern", "An eight-step sequencer. Set the eight sliders, choose how many steps to use, and it loops through them one step every N beats. Optional glide." },
               { "Math", "Combines two modulators - add, subtract, multiply, divide, min, max, average, difference - with gain and offset. Unpatched inputs fall back to a constant." },
            } },
            { "Feedback", {
               { "Feedback", "Outputs the previous frame. Nothing visible on its own - it is the delay that makes a loop legal. See 'Using Feedback' above." },
               { "Trails", "A pre-wired feedback loop: decaying accumulation with drift, zoom, rotation and hue rotation. Reach for this before wiring a loop by hand." },
               { "Reaction-Diffusion", "Gray-Scott chemical simulation, six presets. Needs no input; patch one in and its luminance varies the feed rate so the pattern grows differently through light and dark." },
            } },
            { "Mask", {
               { "Remove Background", "On-device segmentation via the OS - no model download, no network, no key. Subject mode needs macOS 14, Person mode macOS 12. Segmentation is slow, so the mask is computed on demand and cached; for video use auto-refresh, which runs on a beat interval rather than every frame." },
            } },
            { "Resynth", {
               { "Resynthesize", "Each generation reads the previous one, so the image drifts away from the source. The XY pad blends four named mutation effects assigned to its corners; Randomise re-rolls which four. The orb's path can be recorded, looped and replayed in time." },
            } },
            { "Output", {
               { "Output", "Terminal node. Shows the final image, exports a PNG, and records an H.264 .mov at a chosen frame rate. Recording captures the cooked output, so what you see is what is written." },
            } },
         };

         for (const Group& group : groups)
         {
            ImGui::SeparatorText(group.category);
            for (const Entry& entry : group.entries)
            {
               ImGui::Bullet();
               ImGui::TextUnformatted(entry.name);
               ImGui::Indent();
               ImGui::PushTextWrapPos(0.0f);
               ImGui::TextDisabled("%s", entry.text);
               ImGui::PopTextWrapPos();
               ImGui::Unindent();
            }
         }
      }

      if (ImGui::CollapsingHeader("Tips"))
      {
         ImGui::Bullet(); ImGui::TextWrapped("Put a Fit node before a Blend or Layer Stack when your sources are different sizes.");
         ImGui::Bullet(); ImGui::TextWrapped("Modulate a Switcher's 'every' with an LFO for irregular cutting.");
         ImGui::Bullet(); ImGui::TextWrapped("Chain a Math node off two LFOs at different rates to get slow drifting motion.");
         ImGui::Bullet(); ImGui::TextWrapped("A node's output can feed several inputs at once - it only renders once per frame.");
         ImGui::Bullet(); ImGui::TextWrapped("Settings and the last graph layout are stored in ~/Library/Application Support/Infinite.");
      }

      ImGui::End();
   }

   void DisconnectAllTo(INode* dying)
   {
      // a deleted modulator must also be cleared from any Math node feeding on it
      auto* dyingMod = dynamic_cast<IModulator*>(dying);
      auto* dyingFile = dynamic_cast<AudioFileNode*>(dying);
      auto* dyingGeometry = dynamic_cast<IGeometrySource*>(dying);
      auto* dyingXY = dynamic_cast<MacroXYNode*>(dying);
      IModulator* dyingY = dyingXY ? dyingXY->YOutput() : nullptr;
      for (GraphNode& other : gNodes)
      {
         if (auto* render = dynamic_cast<Render3DNode*>(other.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
               if (dyingGeometry != nullptr && render->geometry[slot] == dyingGeometry)
                  render->geometry[slot] = nullptr;
         }
         if (auto* audio = dynamic_cast<AudioAnalyzeNode*>(other.node.get()))
         {
            if (dyingFile != nullptr && audio->fileSource == dyingFile)
               audio->fileSource = nullptr;
         }
         if (auto* math = dynamic_cast<MathNode*>(other.node.get()))
         {
            if ((dyingMod != nullptr && math->inputA == dyingMod) ||
                (dyingY != nullptr && math->inputA == dyingY))
               math->inputA = nullptr;
            if ((dyingMod != nullptr && math->inputB == dyingMod) ||
                (dyingY != nullptr && math->inputB == dyingY))
               math->inputB = nullptr;
         }
         int inputs = InputCountFor(other);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(other, slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
      }
   }

   void RemoveNodeByIndex(int index)
   {
      GraphNode* victim = FindNodeByIndex(index);
      if (victim == nullptr)
         return;
      Modulation::Instance().UnbindAllFor(index);
      gModHistory.erase(index);
      DisconnectAllTo(victim->node.get());
      gNodes.erase(std::remove_if(gNodes.begin(), gNodes.end(),
                                  [index](const GraphNode& g) { return g.index == index; }),
                   gNodes.end());
   }
}

int main()
{
   if (!glfwInit())
   {
      fprintf(stderr, "glfwInit failed\n");
      return 1;
   }

   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
   glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

   GLFWwindow* window = glfwCreateWindow(1600, 1000, "Infinite", nullptr, nullptr);
   if (!window)
   {
      fprintf(stderr, "glfwCreateWindow failed\n");
      glfwTerminate();
      return 1;
   }

   glfwMakeContextCurrent(window);
   glfwSwapInterval(1);

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::StyleColorsDark();

   // A proper UI typeface instead of ImGui's bitmap default. Retina-aware:
   // load at 2x and scale down so text stays sharp on a HiDPI display.
   {
      float xscale = 1.0f, yscale = 1.0f;
      glfwGetWindowContentScale(window, &xscale, &yscale);
      const float baseSize = 15.0f;
      const char* candidates[] = {
         "/System/Library/Fonts/SFNS.ttf",
         "/System/Library/Fonts/HelveticaNeue.ttc",
         "/System/Library/Fonts/Helvetica.ttc",
         "/System/Library/Fonts/Supplemental/Arial.ttf",
      };
      ImGuiIO& io = ImGui::GetIO();
      for (const char* path : candidates)
      {
         if (io.Fonts->AddFontFromFileTTF(path, baseSize * xscale) != nullptr)
         {
            io.FontGlobalScale = 1.0f / xscale;
            break;
         }
      }
   }

   ImGuiStyle& style = ImGui::GetStyle();
   style.FrameRounding = 3.0f;
   style.GrabRounding = 3.0f;
   style.WindowRounding = 4.0f;
   style.ItemSpacing = ImVec2(6, 5);

   ImGui_ImplGlfw_InitForOpenGL(window, true);
   // Installed after the backend so it chains rather than replacing ImGui's.
   glfwSetDropCallback(window, OnFilesDropped);
   ImGui_ImplOpenGL3_Init("#version 150");

   // Cocoa chdir's a bundled app into Contents/Resources, so anything written
   // with a relative path lands inside the .app - which breaks its code
   // signature on first launch and can get the app reported as damaged. Keep
   // all mutable state in Application Support instead.
   std::string settingsDir;
   if (const char* home = getenv("HOME"))
   {
      settingsDir = std::string(home) + "/Library/Application Support/Infinite";
      mkdir(settingsDir.c_str(), 0755); // fine if it already exists
   }
   static std::string iniPath = settingsDir.empty() ? std::string("imgui.ini")
                                                    : settingsDir + "/imgui.ini";
   static std::string graphPath = settingsDir.empty() ? std::string("Infinite.json")
                                                      : settingsDir + "/Infinite.json";
   ImGui::GetIO().IniFilename = iniPath.c_str();

   ed::Config config;
   config.SettingsFile = graphPath.c_str();
   config.EnableSmoothZoom = true; // trackpad momentum made stepped zoom feel jumpy
   gEditor = ed::CreateEditor(&config);

   RegisterNodes();

   // Flat list of every registered type, for the double-click search box.
   std::vector<std::pair<std::string, std::string>> allTypes; // (name, category)
   for (const std::string& category : NodeFactory::Instance().GetCategories())
   {
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
         allTypes.emplace_back(name, category);
   }

   const bool selfTest = getenv("IMAGERESYNTH_SELFTEST") != nullptr;
   if (selfTest)
   {
      // headless smoke test of the whole registry: spawn one of every registered
      // node type, feed every input-taking node from a real source so its shader
      // actually compiles and cooks, then report dimensions.
      for (const auto& t : allTypes)
         SpawnNode(t.first, t.second);

      for (GraphNode& gn : gNodes)
      {
         if (gn.typeName == "Shape")
            gSelfTestFeeder = &gn;
      }
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
               cable->Connect(gSelfTestFeeder->node.get());
         }
      }
   }
   else
   {
      // The canvas starts empty; the dev test modes below need a fixture graph,
      // but a normal launch gives the user a blank patch.
      const bool wantsFixture =
         getenv("INFINITE_RESYNTHTEST") != nullptr ||
         getenv("INFINITE_HIDETEST") != nullptr ||
         getenv("INFINITE_MACROTEST") != nullptr ||
         getenv("INFINITE_RECTEST") != nullptr || getenv("INFINITE_MODTEST") != nullptr ||
         getenv("INFINITE_SIZETEST") != nullptr || getenv("INFINITE_INPUTTEST") != nullptr ||
         getenv("INFINITE_DRAGTEST") != nullptr || getenv("INFINITE_COLORTEST") != nullptr ||
         getenv("INFINITE_PICKERTEST") != nullptr;

      if (getenv("INFINITE_3DTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);
         SpawnNode("Render 3D", "3D", 360.0f, 40.0f);
         auto* g0 = static_cast<GeometryNode*>(gNodes[0].node.get());
         g0->shape = 7; g0->color[0] = 1.0f; g0->color[1] = 0.45f; g0->color[2] = 0.2f;
         g0->uniformScale = 1.5f;
         auto* g1 = static_cast<GeometryNode*>(gNodes[1].node.get());
         g1->shape = 3; g1->posX = 0.9f; g1->posY = -0.35f; g1->uniformScale = 0.7f;
         g1->color[0] = 0.35f; g1->color[1] = 0.7f; g1->color[2] = 1.0f;
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         r->geometry[0] = g0;
         r->geometry[1] = g1;
         r->width = 700.0f; r->height = 700.0f;
         if (getenv("INFINITE_NOCULL") != nullptr)
            r->backfaceCull = false;
         if (getenv("INFINITE_NODEPTH") != nullptr)
            r->depthTest = false;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[0].showParams = false;
         gNodes[1].showParams = false;
      }
      else if (getenv("INFINITE_TEXTFIT") != nullptr)
      {
         SpawnNode("Text", "Text", 40.0f, 40.0f);
         auto* t = static_cast<TextNode*>(gNodes[0].node.get());
         t->text = "naman is a weirdo and this line is deliberately long enough to need several rows";
         t->fontName = "Verdana";
         t->fontSize = 300.0f;   // absurd on purpose: fitting must rein it in
         t->wordWrap = true;
         t->fitToBox = true;
         t->align = 3;
         t->scaleX = 1.4f;
         t->scaleY = 2.2f;
         gNodes[0].showParams = true;
         printf("TEXTFIT fixture: %zu nodes spawned\n", gNodes.size());
      }
      else if (getenv("INFINITE_SHOWCASE4") != nullptr)
      {
         SpawnNode("Reaction Diffusion", "Feedback", 40.0f, 40.0f);
         SpawnNode("Curves", "Color", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Feedback", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Resynth", 1080.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[0].node.get());
         auto* cv = static_cast<CurvesNode*>(gNodes[1].node.get());
         cv->AddPoint(CurvesNode::kRGB, 0.35f, 0.75f);
         cv->AddPoint(CurvesNode::kRGB, 0.7f, 0.2f);
         auto* tr = static_cast<TrailsNode*>(gNodes[3].node.get());
         tr->zoom = 1.02f; tr->rotate = 0.01f; tr->decay = 0.96f;
         auto* rd = static_cast<ReactionDiffusionNode*>(gNodes[0].node.get());
         rd->ApplyPreset(0);
         rd->stepsPerFrame = 24.0f;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
      }
      else if (getenv("INFINITE_SHOWCASE3") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("Macro Knob", "Modulators", 560.0f, 40.0f);
         SpawnNode("Macro XY", "Modulators", 820.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         gNodes[1].showParams = true;
         gNodes[2].showParams = true;
         gNodes[3].showParams = true;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padX = 0.32f;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padY = 0.68f;
      }
      else if (getenv("INFINITE_SHOWCASE2") != nullptr)
      {
         SpawnNode("Noise", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("bloom", "Effects", 560.0f, 40.0f);
         SpawnNode("Switcher", "Compositing", 820.0f, 40.0f);
         SpawnNode("Pattern", "Modulators", 1080.0f, 40.0f);
         SpawnNode("Math", "Modulators", 1340.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[0].node.get());
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_SHOWCASE") != nullptr)
      {
         // dev-only: a representative patch, used to generate the README image
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("glitch", "Effects", 320.0f, 40.0f);
         SpawnNode("Text", "Text", 600.0f, 40.0f);
         SpawnNode("Layer Stack", "Compositing", 880.0f, 40.0f);
         SpawnNode("Output", "Output", 1160.0f, 40.0f);
         SpawnNode("LFO", "Modulators", 320.0f, 560.0f);

         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         shape->shapeType = 6;
         shape->sides = 7;
         shape->size = 0.36f;
         shape->fillColor[0] = 1.0f; shape->fillColor[1] = 0.42f; shape->fillColor[2] = 0.2f;
         auto* txt = static_cast<TextNode*>(gNodes[2].node.get());
         txt->text = "INFINITE";
         txt->fontSize = 150.0f;

         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[3].node.get());

         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
         gNodes[4].showParams = false;
      }
      else if (wantsFixture)
      {
         SpawnNode("Shape", "Source", 60.0f, 60.0f);
         if (getenv("INFINITE_RESYNTHTEST") != nullptr)
         {
            SpawnNode("Resynthesize", "Resynth", 380.0f, 60.0f);
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
            gNodes[1].showParams = true;
         }
         else
            SpawnNode("Output", "Output", 380.0f, 60.0f);
         if (getenv("INFINITE_RECTEST") != nullptr)
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         if (getenv("INFINITE_HIDETEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_MODTEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MACROTEST") != nullptr)
         {
            SpawnNode("Macro XY", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
      }
   }

   // Cocoa chdir's a bundled app to Contents/Resources, so a bare relative path
   // would silently write inside the .app. Default somewhere the user can find.
   char exportPath[512] = "";
   if (const char* home = getenv("HOME"))
      snprintf(exportPath, sizeof(exportPath), "%s/Desktop/infinite_output.png", home);
   else
      snprintf(exportPath, sizeof(exportPath), "infinite_output.png");

   char recordPath[512] = "";
   if (const char* home = getenv("HOME"))
      snprintf(recordPath, sizeof(recordPath), "%s/Desktop/infinite_output.mov", home);
   else
      snprintf(recordPath, sizeof(recordPath), "infinite_output.mov");

   char searchBuf[128] = "";
   bool searchJustOpened = false;
   std::vector<std::string> clipboard;      // typeNames copied
   std::vector<INode*> clipboardSources;    // live sources to copy params from
   int frameId = 0;

   while (!glfwWindowShouldClose(window))
   {
      glfwPollEvents();

      // dev-only: drive copy/paste/delete with synthetic key events so the
      // shortcuts can be verified without a human at the keyboard
      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         auto key = [&tio](ImGuiKey k, bool down) { tio.AddKeyEvent(k, down); };
         switch (frameId)
         {
            case 3: key(ImGuiMod_Super, true); key(ImGuiKey_C, true); break;
            case 4: key(ImGuiKey_C, false); break;
            case 5: key(ImGuiKey_V, true); break;
            case 6: key(ImGuiKey_V, false); key(ImGuiMod_Super, false); break;
            case 8: key(ImGuiKey_Backspace, true); break;
            case 9: key(ImGuiKey_Backspace, false); break;
            default: break;
         }
      }

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();

      // dev-only: click inside the colour picker to prove it is interactive now
      if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true); // headless runs are never OS-focused; ImGui drops input otherwise
         if (frameId >= 8 && gColorPickerRect.z > 0.0f)
            tio.AddMousePosEvent(gColorPickerRect.x + gColorPickerRect.z * 0.25f,
                                 gColorPickerRect.y + gColorPickerRect.w * 0.30f);
         if (frameId == 9)
            tio.AddMouseButtonEvent(0, true);
         if (frameId == 11)
            tio.AddMouseButtonEvent(0, false);
      }

      // dev-only: synthetic mouse drags to prove empty-canvas drag pans the view
      // while a drag that starts on a node still moves that node. The position is
      // re-asserted every frame, otherwise the GLFW backend's real cursor wins on
      // frames we don't touch and the gesture breaks up.
      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         // ImGui normally spreads queued input across frames to preserve click
         // positions; that splits a synthetic gesture apart, so disable it here.
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         switch (frameId)
         {
            // --- phase 1: drag empty canvas (should pan, not move nodes) ---
            case 3: gTestMouse = ImVec2(1400.0f, 800.0f); break;
            case 4: btn(true); break;
            case 5: gTestMouse = ImVec2(1440.0f, 830.0f); break;
            case 6: gTestMouse = ImVec2(1500.0f, 880.0f); break;
            case 7: gTestMouse = ImVec2(1560.0f, 920.0f); break;
            case 8: btn(false); break;
            // --- phase 2: drag the node's title row (should move the node) ---
            case 12: gTestMouse = gDragTestNodeScreen; break;
            case 13: btn(true); break;
            case 14: gTestMouse = ImVec2(gDragTestNodeScreen.x + 40.0f, gDragTestNodeScreen.y + 30.0f); break;
            case 15: gTestMouse = ImVec2(gDragTestNodeScreen.x + 90.0f, gDragTestNodeScreen.y + 70.0f); break;
            case 16: gTestMouse = ImVec2(gDragTestNodeScreen.x + 140.0f, gDragTestNodeScreen.y + 110.0f); break;
            case 17: btn(false); break;
            default: break;
         }
         if (frameId >= 3)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }

      ImGui::NewFrame();

      Transport::Instance().Tick(ImGui::GetIO().DeltaTime);
      Modulation::Instance().ClearFrameParams();
      gDrawnParamPins.clear();
      {
         Modulation& modulation = Modulation::Instance();
         for (GraphNode& gn : gNodes)
            gn.hasModulatedParams = false;
         for (const auto& link : modulation.Links())
         {
            if (GraphNode* target = FindNodeByIndex(link.first.first))
               target->hasModulatedParams = true;
         }
      }

      // ---------------- node editor ----------------
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_MenuBar);

      if (ImGui::BeginMenuBar())
      {
         if (ImGui::BeginMenu("Menu"))
         {
            ImGui::SeparatorText("Canvas");
            ImGui::Checkbox("Snap to grid", &gSnapToGrid);
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Grid size", &gGridSnap, 5.0f, 100.0f, "%.0f px");
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Zoom speed", &gZoomSensitivity, 0.05f, 1.5f, "%.2f");
            if (ImGui::MenuItem("Fit view to content"))
               gRequestFitView = true;

            ImGui::SeparatorText("Nodes");
            if (ImGui::MenuItem("Show all params"))
            {
               for (GraphNode& gn : gNodes)
                  gn.showParams = true;
            }
            if (ImGui::MenuItem("Hide all params"))
            {
               for (GraphNode& gn : gNodes)
                  gn.showParams = false;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Help / module reference"))
               gHelpOpen = true;

            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::EndMenu();
         }
         ImGui::Separator();

         Transport& transport = Transport::Instance();
         ImGui::PushStyleColor(ImGuiCol_Button, transport.IsPlaying()
                                                   ? ImVec4(0.16f, 0.52f, 0.28f, 1.0f)
                                                   : ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
         if (ImGui::Button(transport.IsPlaying() ? "Pause" : "Play", ImVec2(64, 0)))
            transport.TogglePlay();
         ImGui::PopStyleColor();

         if (ImGui::Button("Rewind"))
            transport.Rewind();

         float bpm = transport.Tempo();
         ImGui::SetNextItemWidth(130);
         if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f"))
            transport.SetTempo(bpm);

         ImGui::TextDisabled("bar %d  beat %.2f",
                             1 + (int)(transport.Beats() / 4.0),
                             std::fmod(transport.Beats(), 4.0) + 1.0);

         ImGui::EndMenuBar();
      }

      // The canvas zoomed wildly on a trackpad because it consumes raw wheel
      // deltas; damp them for the duration of the editor, then restore.
      ImGuiIO& io = ImGui::GetIO();
      const float savedWheel = io.MouseWheel;
      const float savedWheelH = io.MouseWheelH;
      io.MouseWheel *= gZoomSensitivity;
      io.MouseWheelH *= gZoomSensitivity;

      ed::SetCurrentEditor(gEditor);

      // Dragging empty canvas should pan, but dragging a node should move it.
      // NavigateAction claims any drag on its configured button regardless of
      // what is underneath, so flip the button per-gesture using last frame's
      // hover state. Shift+drag still gives a rubber-band selection.
      {
         ed::Config& liveCfg = const_cast<ed::Config&>(ed::GetConfig(gEditor));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gHoveringItem && !io.KeyShift)
            gPanWithLeft = true;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gPanWithLeft = false;
         liveCfg.NavigateButtonIndex = gPanWithLeft ? 0 : 1;
      }

      ed::Begin("graph", ImVec2(0.0f, ImGui::GetContentRegionAvail().y));

      if (!gPendingSelect.empty())
      {
         bool first = true;
         for (int nodeId : gPendingSelect)
         {
            ed::SelectNode(nodeId, !first);
            first = false;
         }
         gPendingSelect.clear();
      }

      if (gRequestFitView)
      {
         ed::NavigateToContent(0.0f);
         gRequestFitView = false;
      }

      // Dropping a file on the canvas spawns the matching source node, already
      // loaded, at the drop point.
      if (!gDroppedFiles.empty())
      {
         static const std::vector<std::string> kVideoExt = {
            "mov", "mp4", "m4v", "avi", "mkv", "webm", "mpg", "mpeg", "wmv", "flv", "hevc"
         };
         ImVec2 canvasPos = ed::ScreenToCanvas(gDropPos);
         float offset = 0.0f;
         for (const std::string& path : gDroppedFiles)
         {
            GraphNode* spawned = nullptr;
            if (HasExtension(path, kVideoExt))
            {
               spawned = SpawnNode("Video", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<VideoSourceNode*>(spawned->node.get())->Open(path);
            }
            else
            {
               spawned = SpawnNode("Image Source", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ImageSourceNode*>(spawned->node.get())->Load(path);
            }
            if (spawned != nullptr)
               spawned->showParams = true;
            offset += 240.0f;
         }
         gDroppedFiles.clear();
      }

      if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 7)
            printf("before click: fill=(%.2f, %.2f, %.2f)\n", sh->fillColor[0], sh->fillColor[1], sh->fillColor[2]);
         if (frameId == 11)
         {
            printf("after  click: fill=(%.2f, %.2f, %.2f)  %s\n",
                   sh->fillColor[0], sh->fillColor[1], sh->fillColor[2],
                   (std::fabs(sh->fillColor[0] - 0.9f) > 0.02f ||
                    std::fabs(sh->fillColor[1] - 0.35f) > 0.02f ||
                    std::fabs(sh->fillColor[2] - 0.2f) > 0.02f) ? "PICKER RESPONDED OK" : "NO CHANGE - BUG");
         }
      }

      if (getenv("INFINITE_RECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            out->recordFps = 30;
            bool started = out->StartRecording("/tmp/infinite_rectest.mov");
            printf("start recording: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 5)
         {
            // pause mid-take: the clock must freeze, but frames keep encoding
            Transport::Instance().SetPlaying(false);
            printf("paused at beats=%.4f\n", Transport::Instance().Beats());
         }
         if (frameId == 12)
            printf("beats while paused=%.4f (should be unchanged)\n", Transport::Instance().Beats());
         if (frameId == 13)
            Transport::Instance().SetPlaying(true);
         if (frameId == 40)
         {
            printf("frames captured: %d\n", out->RecordedFrames());
            out->StopRecording();
            printf("stop: %s\n", out->RecordStatus().c_str());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_RESYNTHTEST") != nullptr)
      {
         auto* rs = static_cast<ResynthNode*>(gNodes[1].node.get());
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth() / 3, n->GetOutputHeight() / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         static unsigned char prev[4] = { 0, 0, 0, 0 };
         if (frameId == 2)
         {
            rs->chaos = 0.8f; rs->mutation = 0.8f; rs->sourcePull = 0.02f;
            rs->Randomise();
            sample(rs, prev);
            printf("gen %-3d pixel=(%d,%d,%d)\n", rs->Generation(), prev[0], prev[1], prev[2]);
         }
         if (frameId >= 3 && frameId <= 12)
         {
            rs->StepOnce();
            unsigned char now[4];
            sample(rs, now);
            int drift = abs(now[0]-prev[0]) + abs(now[1]-prev[1]) + abs(now[2]-prev[2]);
            printf("gen %-3d pixel=(%d,%d,%d) drift=%d\n", rs->Generation(), now[0], now[1], now[2], drift);
            memcpy(prev, now, 4);
         }
         if (frameId == 14)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_3DTEST") != nullptr && frameId == 4)
      {
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         const int w = r->GetOutputWidth(), h = r->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         // Count coverage rather than sampling points: a torus knot has holes,
         // and an earlier point-sample test reported "nothing drawn" for a
         // render that was in fact perfectly correct.
         size_t lit = 0;
         for (size_t i = 0; i < px.size(); i += 4)
            if (px[i] + px[i + 1] + px[i + 2] > 60)
               lit++;
         const double coverage = (double)lit / (double)(w * h);
         printf("render %dx%d coverage=%.1f%%  %s\n", w, h, coverage * 100.0,
                (coverage > 0.03 && coverage < 0.95) ? "GEOMETRY RASTERISED OK" : "SUSPECT");
      }

      if (getenv("INFINITE_SIZETEST") != nullptr)
      {
         if (frameId >= 2 && frameId <= 14)
         {
            ImVec2 sz = ed::GetNodeSize(gNodes[0].NodeId());
            printf("f%-2d nodeSize=(%.1f, %.1f)\n", frameId, sz.x, sz.y);
         }
         if (frameId == 15)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         if (frameId == 2)
         {
            gDragTestNodePos = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestViewAnchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            printf("start   node canvas pos = (%.0f, %.0f) viewAnchor=(%.0f,%.0f)\n",
                   gDragTestNodePos.x, gDragTestNodePos.y,
                   gDragTestViewAnchor.x, gDragTestViewAnchor.y);
         }
         if (frameId == 9)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            ImVec2 anchor = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
            bool nodeStill = std::fabs(now.x - gDragTestNodePos.x) < 0.5f &&
                             std::fabs(now.y - gDragTestNodePos.y) < 0.5f;
            bool viewMoved = std::fabs(anchor.x - gDragTestViewAnchor.x) > 20.0f ||
                             std::fabs(anchor.y - gDragTestViewAnchor.y) > 20.0f;
            printf("after canvas drag: node pos=(%.0f,%.0f) viewAnchor=(%.0f,%.0f) -> node %s, view %s : %s\n",
                   now.x, now.y, anchor.x, anchor.y,
                   nodeStill ? "still" : "MOVED",
                   viewMoved ? "panned" : "STATIC",
                   (nodeStill && viewMoved) ? "PAN OK" : "BUG");
         }
         if (frameId == 9)
            ed::NavigateToContent(0.0f); // phase 1 panned the node off-screen
         if (frameId == 11)
         {
            // aim at the node's title row: below the pins, above the preview
            ImVec2 p = ed::GetNodePosition(gNodes[0].NodeId());
            gDragTestNodeScreen = ed::CanvasToScreen(ImVec2(p.x + 60.0f, p.y + 42.0f));
            gDragTestNodePos = p;
         }
         if (frameId == 20)
         {
            ImVec2 now = ed::GetNodePosition(gNodes[0].NodeId());
            printf("after node drag:   node pos = (%.0f, %.0f)  %s\n", now.x, now.y,
                   (std::fabs(now.x - gDragTestNodePos.x) > 20.0f ||
                    std::fabs(now.y - gDragTestNodePos.y) > 20.0f) ? "MOVED OK" : "DID NOT MOVE - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         if (frameId == 2)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            sh->shapeType = 6; // Star, so the paste can be checked for fidelity
            sh->sides = 7;
            ed::SelectNode(gNodes[0].NodeId(), false);
            printf("frame2: selected node0, nodes=%zu\n", gNodes.size());
         }
         if (frameId == 7)
         {
            printf("after paste: nodes=%zu\n", gNodes.size());
            for (GraphNode& gn : gNodes)
            {
               if (auto* sp = dynamic_cast<ShapeNode*>(gn.node.get()))
                  printf("  Shape idx=%d shapeType=%d sides=%d\n", gn.index, sp->shapeType, sp->sides);
            }
            ed::SelectNode(gNodes.back().NodeId(), false);
         }
         if (frameId == 10)
         {
            printf("after delete: nodes=%zu\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      for (GraphNode& gn : gNodes)
      {
         if (gn.needsPosition)
         {
            ed::SetNodePosition(gn.NodeId(), ImVec2(gn.spawnX, gn.spawnY));
            gn.needsPosition = false;
         }

         ed::BeginNode(gn.NodeId());
         ImGui::PushID(gn.index);

         // --- inputs spread along the top edge ---
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            char label[16];
            if (inputs == 1)
               label[0] = '\0';
            else
               snprintf(label, sizeof(label), "%c", 'A' + slot);
            DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
            if (slot + 1 < inputs)
               ImGui::SameLine(0.0f, 12.0f);
         }

         // Group the body so its measured width can right-align the out pin.
         // ed::GetNodeSize() is scaled by the current zoom, so feeding it back
         // into padding inflated the node a little more every frame until it
         // covered the canvas and swallowed every click.
         ImGui::BeginGroup();

         ImGui::TextUnformatted(gn.typeName.c_str());
         ImGui::TextDisabled("%s", gn.category.c_str());

         // --- preview: image for image nodes, a value meter for modulators ---
         const bool multiOutModulator =
            dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr;
         if (multiOutModulator)
            ; // these draw their own meters in the params panel
         else if (auto* mod = dynamic_cast<IModulator*>(gn.node.get()))
            DrawModulatorMeter(mod, gn.index);
         else if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         {
            // Geometry emits a mesh, not a picture: show what it is instead of
            // an empty preview box.
            auto* geo = static_cast<GeometryNode*>(gn.node.get());
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize * 0.45f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + kPreviewSize, origin.y + kPreviewSize * 0.45f);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            const std::string& name = GeometryNode::ShapeNames()[
               std::max(0, std::min(geo->shape, (int)GeometryNode::ShapeNames().size() - 1))];
            dl->AddText(ImVec2(origin.x + 12, origin.y + 14), IM_COL32(200, 206, 226, 255), name.c_str());
            char tris[48];
            snprintf(tris, sizeof(tris), "%zu triangles", geo->TriangleCount());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 34), IM_COL32(130, 136, 156, 255), tris);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 54), IM_COL32(130, 136, 156, 255), "geometry -> Render 3D");
         }
         else if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
            DrawPaintablePreview(draw);
         else
            DrawPreview(gn.node.get());

         // --- params, collapsed behind an eye toggle to keep nodes compact ---
         if (EyeToggle(gn.showParams))
            gn.showParams = !gn.showParams;
         if (!gn.showParams && gn.hasModulatedParams)
         {
            // make it obvious a collapsed node still has live modulation
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "mod");
         }

         BeginNodeParams(gn.index);
         if (gn.showParams)
         {
            if (auto* n = dynamic_cast<ImageSourceNode*>(gn.node.get()))
               DrawImageSourceParams(n);
            else if (auto* n = dynamic_cast<VideoSourceNode*>(gn.node.get()))
               DrawVideoParams(n);
            else if (auto* n = dynamic_cast<FitNode*>(gn.node.get()))
               DrawFitParams(n);
            else if (auto* n = dynamic_cast<LFONode*>(gn.node.get()))
               DrawLFOParams(n);
            else if (auto* n = dynamic_cast<RandomNode*>(gn.node.get()))
               DrawRandomParams(n);
            else if (auto* n = dynamic_cast<PatternNode*>(gn.node.get()))
               DrawPatternParams(n);
            else if (auto* n = dynamic_cast<MathNode*>(gn.node.get()))
               DrawMathParams(n);
            else if (auto* n = dynamic_cast<MacroKnobNode*>(gn.node.get()))
               DrawMacroKnobParams(n);
            else if (auto* n = dynamic_cast<MacroXYNode*>(gn.node.get()))
               DrawMacroXYParams(n);
            else if (auto* n = dynamic_cast<NoiseNode*>(gn.node.get()))
               DrawNoiseParams(n);
            else if (auto* n = dynamic_cast<RampNode*>(gn.node.get()))
               DrawRampParams(n);
            else if (auto* n = dynamic_cast<GeometryNode*>(gn.node.get()))
               DrawGeometryParams(n);
            else if (auto* n = dynamic_cast<Render3DNode*>(gn.node.get()))
               DrawRender3DParams(n);
            else if (auto* n = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
               DrawImageAnalyzeParams(n);
            else if (auto* n = dynamic_cast<AudioFileNode*>(gn.node.get()))
               DrawAudioFileParams(n);
            else if (auto* n = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
               DrawAudioAnalyzeParams(n);
            else if (auto* n = dynamic_cast<ResynthNode*>(gn.node.get()))
               DrawResynthParams(n);
            else if (auto* n = dynamic_cast<CurvesNode*>(gn.node.get()))
               DrawCurvesParams(n);
            else if (auto* n = dynamic_cast<RemoveBgNode*>(gn.node.get()))
               DrawRemoveBgParams(n);
            else if (auto* n = dynamic_cast<DrawNode*>(gn.node.get()))
               DrawDrawParams(n);
            else if (auto* n = dynamic_cast<FeedbackNode*>(gn.node.get()))
               DrawFeedbackParams(n);
            else if (auto* n = dynamic_cast<TrailsNode*>(gn.node.get()))
               DrawTrailsParams(n);
            else if (auto* n = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
               DrawReactionDiffusionParams(n);
            else if (auto* n = dynamic_cast<SwitcherNode*>(gn.node.get()))
               DrawSwitcherParams(n);
            else if (auto* n = dynamic_cast<ShapeNode*>(gn.node.get()))
               DrawShapeParams(n);
            else if (auto* n = dynamic_cast<FormulaNode*>(gn.node.get()))
               DrawFormulaParams(n);
            else if (auto* n = dynamic_cast<TextNode*>(gn.node.get()))
               DrawTextParams(n);
            else if (auto* n = dynamic_cast<LayerStackNode*>(gn.node.get()))
               DrawLayerStackParams(n);
            else if (auto* n = dynamic_cast<BlendNode*>(gn.node.get()))
               DrawBlendParams(n);
            else if (auto* n = dynamic_cast<FilterNode*>(gn.node.get()))
               DrawFilterParams(n);
            else if (auto* n = dynamic_cast<OutputNode*>(gn.node.get()))
            {
               ImGui::SetNextItemWidth(kPreviewSize);
               ImGui::InputText("##png", exportPath, sizeof(exportPath));
               if (ImGui::Button("Export PNG", ImVec2(kPreviewSize, 0)))
                  ExportPng(n, exportPath);

               ImGui::Dummy(ImVec2(0, 4));
               ImGui::SetNextItemWidth(kPreviewSize);
               ImGui::InputText("##mov", recordPath, sizeof(recordPath));
               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::SliderInt("fps", &n->recordFps, 1, 60);

               if (n->IsRecording())
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
                  if (ImGui::Button("Stop recording", ImVec2(kPreviewSize, 0)))
                     n->StopRecording();
                  ImGui::PopStyleColor();
                  ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "REC  %d frames", n->RecordedFrames());
               }
               else
               {
                  if (ImGui::Button("Record video", ImVec2(kPreviewSize, 0)))
                     n->StartRecording(recordPath);
               }
               if (!n->RecordStatus().empty())
               {
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                  ImGui::TextDisabled("%s", n->RecordStatus().c_str());
                  ImGui::PopTextWrapPos();
               }
            }
         }

         ImGui::EndGroup();
         const float contentW = ImGui::GetItemRectSize().x;

         // --- output dots, bottom-right: cables start here ---
         if (dynamic_cast<OutputNode*>(gn.node.get()) == nullptr)
         {
            const int outputs = std::max(1, gn.node->OutputCount());
            float itemW = 0.0f;
            for (int o = 0; o < outputs; o++)
               itemW += kPinHit + 4.0f + ImGui::CalcTextSize(gn.node->OutputLabel(o)).x + (o ? 10.0f : 0.0f);
            float pad = std::max(0.0f, contentW - itemW);
            ImGui::Dummy(ImVec2(pad, 1.0f));
            for (int o = 0; o < outputs; o++)
            {
               ImGui::SameLine(0.0f, o == 0 ? 0.0f : 10.0f);
               DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
            }
         }

         ImGui::PopID();
         ed::EndNode();
      }

      // ---- draw existing links ----
      // Link ids come from this table rather than arithmetic on pin ids, so image
      // cables and modulation cables share one collision-free id space.
      gLinks.clear();
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(gn, slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;

            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                     src.OutputPinId(), gn.InputPinId(slot) });
                  break;
               }
            }
         }
      }
      for (GraphNode& gn : gNodes)
      {
         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
            {
               if (render->geometry[slot] == nullptr)
                  continue;
               for (GraphNode& src : gNodes)
               {
                  if (dynamic_cast<IGeometrySource*>(src.node.get()) == render->geometry[slot])
                  {
                     gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                        src.OutputPinId(), gn.InputPinId(slot) });
                     break;
                  }
               }
            }
         }

         auto* audio = dynamic_cast<AudioAnalyzeNode*>(gn.node.get());
         if (audio != nullptr && audio->fileSource != nullptr)
         {
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == audio->fileSource)
               {
                  gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                     src.OutputPinId(), gn.InputPinId(0) });
                  break;
               }
            }
         }

         auto* math = dynamic_cast<MathNode*>(gn.node.get());
         if (math == nullptr)
            continue;
         for (int slot = 0; slot < 2; slot++)
         {
            IModulator* wanted = (slot == 0) ? math->inputA : math->inputB;
            if (wanted == nullptr)
               continue;
            bool found = false;
            for (GraphNode& src : gNodes)
            {
               for (int o = 0; o < std::max(1, src.node->OutputCount()) && !found; o++)
               {
                  if (ModulatorForOutput(src.node.get(), o) == wanted)
                  {
                     gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                                        src.OutputPinId(o), gn.InputPinId(slot) });
                     found = true;
                  }
               }
               if (found)
                  break;
            }
         }
      }

      for (const auto& link : Modulation::Instance().Links())
      {
         GraphNode* target = FindNodeByIndex(link.first.first);
         GraphNode* source = FindNodeByIndex(link.second.nodeIndex);
         if (target == nullptr || source == nullptr)
            continue;
         const int paramPin = target->ParamPinId(link.first.second);
         if (gDrawnParamPins.count(paramPin) == 0)
            continue; // params collapsed: keep the binding, just don't draw the cable
         gLinks.push_back({ kLinkIdBase + (int)gLinks.size(),
                            source->OutputPinId(link.second.outputIndex), paramPin });
      }

      for (const LinkInfo& link : gLinks)
      {
         const bool isMod = GraphNode::IsParamPin(link.dstPin);
         if (isMod)
            ed::Link(link.id, link.srcPin, link.dstPin, ImColor(255, 190, 90), 1.8f);
         else
            ed::Link(link.id, link.srcPin, link.dstPin);
      }

      // ---- handle new connections ----
      if (ed::BeginCreate())
      {
         ed::PinId startPin, endPin;
         if (ed::QueryNewLink(&startPin, &endPin))
         {
            if (startPin && endPin)
            {
               int a = (int)startPin.Get();
               int b = (int)endPin.Get();

               // normalize so `a` is the output side
               if (!GraphNode::IsOutputPin(a))
                  std::swap(a, b);

               GraphNode* srcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(a));
               GraphNode* dstNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(b));
               const bool differentNodes = GraphNode::NodeIndexFromPin(a) != GraphNode::NodeIndexFromPin(b);
               const bool srcIsModulator = srcNode != nullptr &&
                                           dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr;

               auto* dstMath = dstNode ? dynamic_cast<MathNode*>(dstNode->node.get()) : nullptr;
               auto* dstAudio = dstNode ? dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get()) : nullptr;
               auto* srcAudioFile = srcNode ? dynamic_cast<AudioFileNode*>(srcNode->node.get()) : nullptr;
               auto* dstRender = dstNode ? dynamic_cast<Render3DNode*>(dstNode->node.get()) : nullptr;
               auto* srcGeometry = srcNode ? dynamic_cast<IGeometrySource*>(srcNode->node.get()) : nullptr;

               bool valid = false;
               if (GraphNode::IsOutputPin(a) && srcNode != nullptr && dstNode != nullptr && differentNodes)
               {
                  // modulators patch into parameters and into Math's inputs;
                  // image nodes patch into image inputs
                  const bool dstWantsImage =
                     dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) != nullptr;

                  if (GraphNode::IsParamPin(b))
                     valid = srcIsModulator;
                  else if (GraphNode::IsInputPin(b))
                  {
                     if (dstRender != nullptr)
                        valid = srcGeometry != nullptr; // Render 3D only accepts geometry
                     else if (srcGeometry != nullptr)
                        valid = false;                  // geometry only goes into Render 3D
                     else if (dstAudio != nullptr)
                        valid = srcAudioFile != nullptr; // only an Audio File feeds Audio Analyze
                     else
                        valid = (dstMath != nullptr && !dstWantsImage) ? srcIsModulator : !srcIsModulator;
                  }
               }

               if (valid && ed::AcceptNewItem())
               {
                  if (GraphNode::IsParamPin(b))
                  {
                     Modulation::Instance().Bind(dstNode->index,
                                                 GraphNode::ParamIndexFromPin(b),
                                                 srcNode->index,
                                                 GraphNode::OutputIndexFromPin(a));
                  }
                  else if (dstRender != nullptr)
                  {
                     dstRender->geometry[GraphNode::InputSlotFromPin(b)] = srcGeometry;
                  }
                  else if (dstAudio != nullptr)
                  {
                     dstAudio->fileSource = srcAudioFile;
                  }
                  else if (dstMath != nullptr)
                  {
                     auto* mod = ModulatorForOutput(srcNode->node.get(), GraphNode::OutputIndexFromPin(a));
                     if (GraphNode::InputSlotFromPin(b) == 0)
                        dstMath->inputA = mod;
                     else
                        dstMath->inputB = mod;
                  }
                  else
                  {
                     ImageCable* cable = CableFor(*dstNode, GraphNode::InputSlotFromPin(b));
                     if (cable != nullptr)
                        cable->Connect(srcNode->node.get());
                  }
               }
               else if (!valid)
               {
                  ed::RejectNewItem(ImColor(255, 80, 80), 2.0f);
               }
            }
         }
      }
      ed::EndCreate();

      // ---- keyboard: delete + copy/paste ----
      const bool typing = io.WantTextInput;
      const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;

      if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
      {
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            std::vector<ed::LinkId> selLinks(count);
            int linkCount = ed::GetSelectedLinks(selLinks.data(), count);

            for (int i = 0; i < linkCount; i++)
            {
               DisconnectLinkById((int)selLinks[i].Get());
               ed::DeleteLink(selLinks[i]);
            }
            for (int i = 0; i < nodeCount; i++)
            {
               ed::DeleteNode(selNodes[i]);
               RemoveNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
            }
            ed::ClearSelection();
         }
      }

      // Shift+D duplicates whatever is selected without touching the clipboard.
      if (!typing && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false))
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // Resolve everything first: SpawnNode can reallocate gNodes.
            struct DupItem { std::string type; std::string category; INode* src; ImVec2 pos; bool params; };
            std::vector<DupItem> items;
            for (int i = 0; i < nodeCount; i++)
            {
               if (GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride))
               {
                  const ImVec2 p = ed::GetNodePosition(gn->NodeId());
                  items.push_back({ gn->typeName, gn->category, gn->node.get(),
                                    ImVec2(p.x + 40.0f, p.y + 40.0f), gn->showParams });
               }
            }

            ed::ClearSelection();
            for (const DupItem& item : items)
            {
               if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
               {
                  CopyParams(copy->node.get(), item.src);
                  copy->showParams = item.params;
                  gPendingSelect.push_back(copy->NodeId());
               }
            }
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
      {
         clipboard.clear();
         clipboardSources.clear();
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            for (int i = 0; i < nodeCount; i++)
            {
               if (GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride))
               {
                  clipboard.push_back(gn->typeName);
                  clipboardSources.push_back(gn->node.get());
               }
            }
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard.empty())
      {
         // resolve sources first: SpawnNode can reallocate gNodes and invalidate pointers
         struct PasteItem { std::string type; std::string category; INode* src; ImVec2 pos; };
         std::vector<PasteItem> items;
         for (size_t i = 0; i < clipboard.size(); i++)
         {
            for (GraphNode& gn : gNodes)
            {
               if (gn.node.get() == clipboardSources[i])
               {
                  ImVec2 p = ed::GetNodePosition(gn.NodeId());
                  items.push_back({ clipboard[i], gn.category, gn.node.get(),
                                    ImVec2(p.x + 40.0f, p.y + 40.0f) });
                  break;
               }
            }
         }
         for (const PasteItem& item : items)
         {
            GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y);
            if (copy != nullptr)
               CopyParams(copy->node.get(), item.src);
         }
      }

      // ---- handle deletions raised by the editor itself ----
      if (ed::BeginDelete())
      {
         ed::LinkId linkId;
         while (ed::QueryDeletedLink(&linkId))
         {
            if (ed::AcceptDeletedItem())
               DisconnectLinkById((int)linkId.Get());
         }

         ed::NodeId nodeId;
         while (ed::QueryDeletedNode(&nodeId))
         {
            if (ed::AcceptDeletedItem())
               RemoveNodeByIndex((int)nodeId.Get() / GraphNode::kStride);
         }
      }
      ed::EndDelete();

      // ---- snap to grid once the drag finishes ----
      if (gSnapToGrid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         for (GraphNode& gn : gNodes)
         {
            ImVec2 p = ed::GetNodePosition(gn.NodeId());
            ImVec2 snapped(std::round(p.x / gGridSnap) * gGridSnap,
                           std::round(p.y / gGridSnap) * gGridSnap);
            if (std::fabs(snapped.x - p.x) > 0.01f || std::fabs(snapped.y - p.y) > 0.01f)
               ed::SetNodePosition(gn.NodeId(), snapped);
         }
      }

      // ---- popups: search, spawn menu, dropdown ----
      ed::Suspend();

      // Right-click (two-finger click on a Mac trackpad) opens the same
      // type-to-filter picker as double-click, so the keyboard works either way.
      if (ed::ShowBackgroundContextMenu())
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // double-click empty canvas -> searchable spawner
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
          !ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink())
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // dev-only: force the picker open (optionally with a query) to verify layout
      if (const char* pk = getenv("INFINITE_PICKERTEST"))
      {
         if (frameId == 6)
         {
            gSpawnPos = ImVec2(200.0f, 200.0f);
            snprintf(searchBuf, sizeof(searchBuf), "%s", std::string(pk) == "empty" ? "" : pk);
            searchJustOpened = (std::string(pk) == "empty");
            ImGui::OpenPopup("search");
         }
      }

      if (gDropdown.justOpened)
      {
         ImGui::OpenPopup("##dropdown");
         gDropdown.justOpened = false;
      }

      if (getenv("INFINITE_COLORTEST") != nullptr && frameId == 6)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         sh->fillColor[0] = 0.9f; sh->fillColor[1] = 0.35f; sh->fillColor[2] = 0.2f;
         gColor.target = sh->fillColor;
         gColor.owner = sh;
         gColor.label = "fill";
         gColor.justOpened = true;
      }

      if (gColor.justOpened)
      {
         ImGui::OpenPopup("##colorpick");
         gColor.justOpened = false;
      }

      // the owning node may have been deleted while the picker was open
      if (gColor.owner != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gColor.owner)
               alive = true;
         }
         if (!alive)
         {
            gColor.owner = nullptr;
            gColor.target = nullptr;
         }
      }

      if (ImGui::BeginPopup("##colorpick"))
      {
         if (gColor.target != nullptr)
         {
            ImGui::TextDisabled("%s", gColor.label.c_str());
            gColorPickerRect = ImVec4(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y,
                                      0.0f, 0.0f);
            ImGui::ColorPicker3("##pick", gColor.target,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_DisplayHSV |
                                ImGuiColorEditFlags_DisplayHex);
            gColorPickerRect.z = ImGui::GetItemRectSize().x;
            gColorPickerRect.w = ImGui::GetItemRectSize().y;
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(400, 440));
      if (ImGui::BeginPopup("search"))
      {
         if (searchJustOpened)
         {
            ImGui::SetKeyboardFocusHere();
            searchJustOpened = false;
         }
         ImGui::SetNextItemWidth(280);
         ImGui::InputTextWithHint("##q", "search nodes...", searchBuf, sizeof(searchBuf));
         ImGui::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         if (q.empty())
         {
            // no query yet: browse by category
            for (const std::string& category : NodeFactory::Instance().GetCategories())
            {
               ImGui::SeparatorText(category.c_str());
               for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
               {
                  ++shown;
                  if (ImGui::Selectable(name.c_str()))
                  {
                     spawnName = name;
                     spawnCategory = category;
                  }
               }
            }
         }
         else
         {
            for (const auto& t : allTypes)
            {
               std::string hay = t.first + " " + t.second;
               std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
               if (hay.find(q) == std::string::npos)
                  continue;
               ++shown;
               std::string entry = t.first + "   (" + t.second + ")";
               bool activate = ImGui::Selectable(entry.c_str());
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("no matches");
         }

         if (!spawnName.empty())
         {
            SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(220, 0), ImVec2(420, 400));
      if (ImGui::BeginPopup("##dropdown"))
      {
         if (gDropdown.options != nullptr)
         {
            for (int i = 0; i < (int)gDropdown.options->size(); i++)
            {
               bool selected = (i == gDropdown.current);
               if (ImGui::Selectable((*gDropdown.options)[i].c_str(), selected))
               {
                  if (gDropdown.onSelect)
                     gDropdown.onSelect(i);
                  ImGui::CloseCurrentPopup();
               }
               if (selected && ImGui::IsWindowAppearing())
                  ImGui::SetScrollHereY(0.5f);
            }
         }
         ImGui::EndPopup();
      }

      gHoveringItem = ed::GetHoveredNode() || ed::GetHoveredPin() || ed::GetHoveredLink();

      ed::Resume();

      ed::End();
      ed::SetCurrentEditor(nullptr);

      io.MouseWheel = savedWheel;
      io.MouseWheelH = savedWheelH;

      ImGui::End();

      // ---- windows that must live outside the node canvas ----
      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         // guard against the node being deleted while its editor is open
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFormulaEditor)
               alive = true;
         }
         if (!alive)
         {
            gFormulaEditor = nullptr;
            gFormulaEditorOpen = false;
         }
      }

      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("Formula editor", &gFormulaEditorOpen))
         {
            ImGui::TextDisabled("body of  vec4 shape(vec2 uv, vec2 p, float t)");
            ImGui::TextDisabled("p is centred (-0.5..0.5), t is transport seconds, uA-uD are the knobs");
            ImGui::Separator();

            static char editBuf[8192];
            static FormulaNode* lastEdited = nullptr;
            if (lastEdited != gFormulaEditor)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());
               lastEdited = gFormulaEditor;
            }

            ImGui::InputTextMultiline("##glsl", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 70));

            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFormulaEditor->formula = editBuf;
               gFormulaEditor->Apply();
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());

            if (!gFormulaEditor->LastError().empty())
            {
               ImGui::TextWrapped("%s", gFormulaEditor->LastError().c_str());
            }
         }
         ImGui::End();
      }

      if (gHelpOpen)
         DrawHelpWindow(&gHelpOpen);

      // ---- apply modulation, then cook ----
      // Deliberately after the UI: the parameter registry is rebuilt every frame
      // while nodes draw, so every pointer here belongs to a node that still
      // exists. Cooking before the UI would mean writing through last frame's
      // pointers, which dangle the moment a node is deleted.
      {
         Modulation& modulation = Modulation::Instance();
         for (const ParamRef& ref : modulation.FrameParams())
         {
            const Modulation::Source src = modulation.ModulatorFor(ref.nodeIndex, ref.paramIndex);
            if (src.nodeIndex < 0 || ref.value == nullptr)
               continue;
            GraphNode* modNode = FindNodeByIndex(src.nodeIndex);
            if (modNode == nullptr)
               continue;
            auto* modulator = ModulatorForOutput(modNode->node.get(), src.outputIndex);
            if (modulator == nullptr)
               continue;
            const float v01 = modulator->Value01();
            *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
         }
      }

      for (GraphNode& gn : gNodes)
      {
         if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr)
            gn.node->CookIfNeeded(frameId);
      }
      if (getenv("INFINITE_SHOWCASE") != nullptr && frameId == 1)
      {
         for (const ParamRef& ref : Modulation::Instance().FrameParams())
         {
            if (ref.nodeIndex == gNodes[1].index && ref.name == "Amount")
               Modulation::Instance().Bind(ref.nodeIndex, ref.paramIndex, gNodes[5].index);
         }
      }

      if (getenv("INFINITE_MACROTEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (gNodes.size() < 3)
         {
            printf("MACROTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* xy = static_cast<MacroXYNode*>(gNodes[2].node.get());
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 2)
         {
            int sizeParam = -1, rotParam = -1;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex != gNodes[0].index) continue;
               if (ref.name == "size") sizeParam = ref.paramIndex;
               if (ref.name == "rotation") rotParam = ref.paramIndex;
            }
            // X drives size, Y drives rotation - one pad, two destinations
            printf("sizeParam=%d rotParam=%d node0=%d node2=%d frameParams=%zu\n",
                   sizeParam, rotParam, gNodes[0].index, gNodes[2].index, mod.FrameParams().size());
            mod.Bind(gNodes[0].index, sizeParam, gNodes[2].index, 0);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index, 1);
            printf("isModulated(size)=%d isModulated(rot)=%d\n",
                   (int)mod.IsModulated(gNodes[0].index, sizeParam),
                   (int)mod.IsModulated(gNodes[0].index, rotParam));
            xy->padX = 0.25f;
            xy->padY = 0.75f;
            printf("bound X->size Y->rotation\n");
         }
         if (frameId == 5)
         {
            printf("padX=%.2f -> size=%.4f (expect %.4f)\n", xy->padX, sh->size, 0.01f + 0.49f * 0.25f);
            printf("padY=%.2f -> rotation=%.4f (expect %.4f)\n", xy->padY, sh->rotation, -3.1416f + 6.2832f * 0.75f);
            xy->padX = 0.9f; xy->padY = 0.1f;
         }
         if (frameId == 8)
         {
            printf("after move: size=%.4f rotation=%.4f  %s\n", sh->size, sh->rotation,
                   (std::fabs(sh->size - (0.01f + 0.49f * 0.9f)) < 0.01f &&
                    std::fabs(sh->rotation - (-3.1416f + 6.2832f * 0.1f)) < 0.05f)
                      ? "INDEPENDENT OUTPUTS OK" : "MISMATCH");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_HIDETEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (frameId == 2)
         {
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size")
                  mod.Bind(ref.nodeIndex, ref.paramIndex, gNodes[2].index);
            }
            printf("bound: links=%zu (frameParams=%zu)\n", mod.Links().size(), mod.FrameParams().size());
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index)
                  printf("   node0 param %d = %s\n", ref.paramIndex, ref.name.c_str());
            }
         }
         if (frameId == 4)
         {
            gNodes[0].showParams = false; // collapse the modulated node
            printf("params hidden\n");
         }
         if (frameId == 8)
            printf("after hide: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
         if (frameId == 9)
         {
            gNodes[0].showParams = true; // reopen
         }
         if (frameId == 12)
         {
            printf("after reopen: links=%zu %s\n", mod.Links().size(),
                   mod.Links().empty() ? "LOST - BUG" : "SURVIVED OK");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (getenv("INFINITE_MODTEST") != nullptr)
      {
         if (frameId == 1)
         {
            // Shape.size is param index 4 (shape dropdown isn't a ModSlider):
            // width, height, size, aspect... resolve it by name instead of guessing.
            int sizeParam = -1;
            for (const ParamRef& ref : Modulation::Instance().FrameParams())
            {
               if (ref.nodeIndex == gNodes[0].index && ref.name == "size")
                  sizeParam = ref.paramIndex;
            }
            printf("size param index = %d\n", sizeParam);
            if (sizeParam >= 0)
               Modulation::Instance().Bind(gNodes[0].index, sizeParam, gNodes[2].index);
            Transport::Instance().SetTempo(240.0f);
         }
         if (frameId >= 2 && frameId <= 40 && (frameId % 8) == 0)
         {
            auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
            auto* lfo = static_cast<LFONode*>(gNodes[2].node.get());
            printf("f%-3d beats=%.3f lfo=%.3f  shape.size=%.4f\n",
                   frameId, Transport::Instance().Beats(), lfo->Value01(), sh->size);
         }
         if (frameId == 44)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      for (GraphNode& gn : gNodes)
         gn.node->CookIfNeeded(frameId);

      if (getenv("INFINITE_TEXTFIT") != nullptr && frameId == 4 && !gNodes.empty())
      {
         if (auto* t = dynamic_cast<TextNode*>(gNodes[0].node.get()))
            printf("requested %.0f pt -> fitted %.1f pt (box %.0fx%.0f of %dx%d)\n",
                   t->fontSize, t->FittedSize(),
                   t->width * t->wrapWidth, t->height * t->wrapHeight,
                   t->GetOutputWidth(), t->GetOutputHeight());
      }

      if (selfTest && frameId >= 1)
      {
         int failures = 0;
         for (GraphNode& gn : gNodes)
         {
            // modulators emit a value, not a texture, so they are checked
            // differently - including nodes that expose taps rather than being
            // modulators themselves (Image/Audio Analyze).
            if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
            {
               // geometry emits a mesh, not a texture
               const bool ok = geo->TriangleCount() > 0;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), geo->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }

            if (dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr)
            {
               // an audio source has neither a texture nor a modulator tap
               printf("%-22s [%-12s] audio source   OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }

            IModulator* mod = dynamic_cast<IModulator*>(gn.node.get());
            if (mod == nullptr)
               mod = gn.node->ModulatorOutput(0);
            if (mod != nullptr)
            {
               const float v = mod->Value01();
               const bool ok = v >= 0.0f && v <= 1.0f;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] value=%.3f      %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), v, ok ? "OK" : "FAIL");
               continue;
            }

            bool ok = gn.node->GetOutputWidth() > 0 && gn.node->GetOutputTexture() != 0;
            if (!ok)
               ++failures;
            printf("%-22s [%-12s] %dx%d tex=%-3u %s\n",
                   gn.typeName.c_str(), gn.category.c_str(),
                   gn.node->GetOutputWidth(), gn.node->GetOutputHeight(),
                   gn.node->GetOutputTexture(), ok ? "OK" : "FAIL");
         }
         printf("\n%zu node types, %d failures\n", gNodes.size(), failures);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }



      ImGui::Render();
      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

      if (const char* shotPath = getenv("IMAGERESYNTH_SCREENSHOT"))
      {
         if (frameId == 12)
         {
            std::vector<unsigned char> px(fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            stbi_write_png(shotPath, fbW, fbH, 4, px.data(), fbW * 4);
            printf("wrote %s (%dx%d)\n", shotPath, fbW, fbH);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      glfwSwapBuffers(window);
      ++frameId;
   }

   gNodes.clear();
   ed::DestroyEditor(gEditor);
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
   glfwDestroyWindow(window);
   glfwTerminate();
   return 0;
}
