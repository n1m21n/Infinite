// Sketch node body: Edit button, error line, size, and the knobs that the script's
// param() calls declared (modulatable like Field's, see docs/plans/sketch/README.md).
#include "app/AppShared.h"

namespace app
{
   void DrawSketchParams(SketchNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         if (ImGui::Button("Edit Sketch...", ImVec2(kPreviewSize, 0)))
         {
            gSketchEditor = n;
            gSketchEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }
      }

      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModCheckbox("animate", &n->animate);

      DrawFieldParamSliders(n);
   }
}
