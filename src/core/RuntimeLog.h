#pragma once

#include <string>

namespace RuntimeLog
{
   void Initialize(bool enabled);
   void SetEnabled(bool enabled);
   bool Enabled();
   void Write(const char* format, ...);
   void Clear();
   std::string Path();
}
