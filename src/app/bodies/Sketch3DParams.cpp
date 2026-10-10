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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", n->LastError().c_str());
            ImGui::PopTextWrapPos();
         }

         if (!n->Notice().empty())
         {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tok::U32(tok::modulation)), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }
      }

      ModCheckbox("animate", &n->animate);

      DrawFieldParamSliders(n);
   }
}
