#include "app/ui/design/UiType.h"

namespace UiType
{
   namespace
   {
      ImFont* sFonts[(int)Weight::Count] = {};
   }

   void Reset()
   {
      for (ImFont*& f : sFonts)
         f = nullptr;
   }

   void Register(Weight w, ImFont* f)
   {
      sFonts[(int)w] = f;
   }

   ImFont* Font(Weight w)
   {
      if (ImFont* f = sFonts[(int)w])
         return f;
      if (ImFont* f = sFonts[(int)Weight::Regular])
         return f;
      return ImGui::GetFont();
   }

   int Registered()
   {
      int n = 0;
      for (ImFont* f : sFonts)
         n += f != nullptr;
      return n;
   }

   void Push(Size s, Weight w)
   {
      ImGui::PushFont(Font(w), Px(s));
   }

   void Pop()
   {
      ImGui::PopFont();
   }
}
