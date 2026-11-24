// Sketch 3D node body: Edit button, error line, animate, and the knobs that the script's
// param() calls declared (modulatable like Sketch's).
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"

namespace app
{
   void DrawSketch3DParams(Sketch3DNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         if (ActionButton::Draw("Edit Sketch 3D...", ImVec2(kPreviewSize, 0)))
         {
            gSketch3DEditor = n;
            gSketch3DEditorOpen = true;
         }

         if (!n->LastError().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(tok::V4(tok::sketch3_d_params_col_2), "%s", n->LastError().c_str());
            PopTextWrapWidth();
         }

         if (!n->Notice().empty())
         {
            PushTextWrapWidth(kPreviewSize);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::modulation)), "%s", n->Notice().c_str());
            PopTextWrapWidth();
         }
      }

      ModCheckbox("animate", &n->animate);

      DrawFieldParamSliders(n);
   }
}
