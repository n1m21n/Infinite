#pragma once

#include "FieldError.h"
#include <string>
#include <vector>

namespace Field
{
   enum class TokenKind
   {
      EndOfFile,
      Number,
      Ident,
      Keyword,
      Op,
      Punct,
      Newline,
      Comment,
      String // D7 (step 10, graph domain): quoted string literal. The only
             // place a string appears in Field - never a value type, never
             // bound to a name. `text` holds the unescaped content.
   };

   struct Token
   {
      TokenKind kind = TokenKind::EndOfFile;
      SourceSpan span;
      double numberValue = 0.0;
      std::string text;

      bool IsOp(const std::string& opStr) const
      {
         return kind == TokenKind::Op && text == opStr;
      }

      bool IsPunct(char c) const
      {
         return kind == TokenKind::Punct && text.size() == 1 && text[0] == c;
      }

      bool IsKeyword(const std::string& kw) const
      {
         return kind == TokenKind::Keyword && text == kw;
      }

      bool IsIdent(const std::string& id) const
      {
         return kind == TokenKind::Ident && text == id;
      }
   };

   bool Lex(const std::string& src, std::vector<Token>& tokens, FieldError& error);
}
