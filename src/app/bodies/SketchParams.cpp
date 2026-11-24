// Sketch node body: Edit button, error line, size, and the knobs that the script's
// param() calls declared (modulatable like Field's, see docs/plans/sketch/README.md).
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"

namespace app
{
   void DrawSketchParams(SketchNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         if (ActionButton::Draw("Edit Sketch...", ImVec2(kPreviewSize, 0)))
         {
            gSketchEditor = n;
            gSketchEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::V4(tok::sketch_params_col_2), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::modulation)), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }
      }

      ModSlider("width", &n->width, 16.0f, 4096.0f, "%.0f");
      ModSlider("height", &n->height, 16.0f, 4096.0f, "%.0f");
      ModCheckbox("animate", &n->animate);

      DrawFieldParamSliders(n);
   }
}
