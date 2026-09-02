#include "FieldLex.h"

#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace Field
{
   namespace
   {
      static const std::unordered_set<std::string> kKeywords = {
         "attrib", "param", "state", "if", "else", "for",
         "float", "int", "bool", "vec2", "vec3", "vec4",
         "true", "false"
      };

      bool IsKeyword(const std::string& str)
      {
         return kKeywords.find(str) != kKeywords.end();
      }
   }

   bool Lex(const std::string& src, std::vector<Token>& tokens, FieldError& error)
   {
      tokens.clear();
      error.Clear();

      size_t pos = 0;
      int line = 1;
      int col = 1;

      auto peek = [&](size_t offset = 0) -> char {
         size_t idx = pos + offset;
         return idx < src.size() ? src[idx] : '\0';
      };

      auto advance = [&]() -> char {
         if (pos >= src.size()) return '\0';
         char c = src[pos++];
         if (c == '\n')
         {
            line++;
            col = 1;
         }
         else
         {
            col++;
         }
         return c;
      };

      while (pos < src.size())
      {
         char c = peek();

         // Whitespace (except newline which can be significant in some contexts)
         if (c == ' ' || c == '\t' || c == '\r')
         {
            advance();
            continue;
         }

         if (c == '\n')
         {
            size_t tokenOffset = pos;
            int tokenLine = line;
            int tokenCol = col;
            advance();
            Token tok;
            tok.kind = TokenKind::Newline;
            tok.span = { tokenOffset, tokenLine, tokenCol, 1 };
            tok.text = "\n";
            tokens.push_back(tok);
            continue;
         }

         // Comment: # to end of line
         if (c == '#')
         {
            size_t tokenOffset = pos;
            int tokenLine = line;
            int tokenCol = col;
            std::string text;
            while (pos < src.size() && peek() != '\n')
            {
               text += advance();
            }
            Token tok;
            tok.kind = TokenKind::Comment;
            tok.span = { tokenOffset, tokenLine, tokenCol, text.size() };
            tok.text = text;
            tokens.push_back(tok);
            continue;
         }

         size_t tokenOffset = pos;
         int tokenLine = line;
         int tokenCol = col;

         // Numbers: [0-9]+ ( . [0-9]* )? ( [eE] [+-]? [0-9]+ )? or . [0-9]+
         if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(1))))
         {
            size_t start = pos;
            bool hasDot = false;
            if (c == '.')
            {
               hasDot = true;
               advance(); // consume '.'
            }
            while (isdigit((unsigned char)peek()))
            {
               advance();
            }
            if (!hasDot && peek() == '.' && isdigit((unsigned char)peek(1)))
            {
               hasDot = true;
               advance(); // consume '.'
               while (isdigit((unsigned char)peek()))
               {
                  advance();
               }
            }
            if (peek() == 'e' || peek() == 'E')
            {
               char next = peek(1);
               if (isdigit((unsigned char)next) || ((next == '+' || next == '-') && isdigit((unsigned char)peek(2))))
               {
                  advance(); // consume e/E
                  if (peek() == '+' || peek() == '-') advance();
                  while (isdigit((unsigned char)peek()))
                  {
                     advance();
                  }
               }
            }
            size_t len = pos - start;
            std::string numStr = src.substr(start, len);
            char* endPtr = nullptr;
            double val = std::strtod(numStr.c_str(), &endPtr);

            Token tok;
            tok.kind = TokenKind::Number;
            tok.span = { tokenOffset, tokenLine, tokenCol, len };
            tok.numberValue = val;
            tok.text = numStr;
            tokens.push_back(tok);
            continue;
         }

         // Identifiers / Keywords: [A-Za-z_][A-Za-z0-9_]*
         if (isalpha((unsigned char)c) || c == '_')
         {
            size_t start = pos;
            while (isalnum((unsigned char)peek()) || peek() == '_')
            {
               advance();
            }
            size_t len = pos - start;
            std::string text = src.substr(start, len);
            Token tok;
            tok.span = { tokenOffset, tokenLine, tokenCol, len };
            tok.text = text;
            if (IsKeyword(text))
            {
               tok.kind = TokenKind::Keyword;
               if (text == "true") tok.numberValue = 1.0;
               else if (text == "false") tok.numberValue = 0.0;
            }
            else
            {
               tok.kind = TokenKind::Ident;
            }
            tokens.push_back(tok);
            continue;
         }

         // Multi-character and single-character operators
         // Maximal munch: <=, >=, ==, !=, &&, ||, +=, -=, *=, /=
         char next = peek(1);
         if ((c == '<' && next == '=') ||
             (c == '>' && next == '=') ||
             (c == '=' && next == '=') ||
             (c == '!' && next == '=') ||
             (c == '&' && next == '&') ||
             (c == '|' && next == '|') ||
             (c == '+' && next == '=') ||
             (c == '-' && next == '=') ||
             (c == '*' && next == '=') ||
             (c == '/' && next == '='))
         {
            std::string opStr;
            opStr += advance();
            opStr += advance();
            Token tok;
            tok.kind = TokenKind::Op;
            tok.span = { tokenOffset, tokenLine, tokenCol, 2 };
            tok.text = opStr;
            tokens.push_back(tok);
            continue;
         }

         // Single character operators: + - * / % ^ < > = ! .
         if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
             c == '^' || c == '<' || c == '>' || c == '=' || c == '!' || c == '.')
         {
            advance();
            Token tok;
            tok.kind = TokenKind::Op;
            tok.span = { tokenOffset, tokenLine, tokenCol, 1 };
            tok.text = std::string(1, c);
            tokens.push_back(tok);
            continue;
         }

         // Punctuation: ( ) { } , [ ] ;
         if (c == '(' || c == ')' || c == '{' || c == '}' ||
             c == ',' || c == '[' || c == ']' || c == ';')
         {
            advance();
            Token tok;
            tok.kind = TokenKind::Punct;
            tok.span = { tokenOffset, tokenLine, tokenCol, 1 };
            tok.text = std::string(1, c);
            tokens.push_back(tok);
            continue;
         }

         // Unexpected character
         error.severity = Severity::Error;
         error.span = { tokenOffset, tokenLine, tokenCol, 1 };
         error.message = "unexpected character";
         return false;
      }

      Token eof;
      eof.kind = TokenKind::EndOfFile;
      eof.span = { pos, line, col, 0 };
      eof.text = "";
      tokens.push_back(eof);
      return true;
   }
}
