#pragma once

#include <string>
#include <cstddef>

namespace Field
{
   enum class Severity
   {
      Error,
      Warning
   };

   struct SourceSpan
   {
      size_t offset = 0;
      int line = 1;
      int col = 1;
      size_t length = 0;
   };

   struct FieldError
   {
      Severity severity = Severity::Error;
      SourceSpan span;
      std::string message;
      std::string hint;

      bool Empty() const { return message.empty(); }
      void Clear()
      {
         message.clear();
         hint.clear();
         span = SourceSpan{};
         severity = Severity::Error;
      }
   };
}
