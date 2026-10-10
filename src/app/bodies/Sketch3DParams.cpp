// Sketch 3D node body: Edit button, error line, animate, and the knobs that the script's
// param() calls declared (modulatable like Sketch's).
#include "app/AppShared.h"

namespace app
{
   void DrawSketch3DParams(Sketch3DNode* n)
   {
      n->SetNodeIndex(gCurrentNodeIndex);

      if (!gParamRegisterOnly)
      {
         if (ImGui::Button("Edit Sketch 3D...", ImVec2(kPreviewSize, 0)))
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
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", n->Notice().c_str());
            ImGui::PopTextWrapPos();
         }
      }

      ModCheckbox("animate", &n->animate);

      DrawFieldParamSliders(n);
   }
}
