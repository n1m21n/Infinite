#include "FieldParse.h"

namespace Field
{
   namespace
   {
      struct ParserState
      {
         const std::vector<Token>& tokens;
         size_t index = 0;
         FieldError& error;

         ParserState(const std::vector<Token>& toks, FieldError& err)
            : tokens(toks), error(err) {}

         bool AtEnd() const
         {
            return index >= tokens.size() || tokens[index].kind == TokenKind::EndOfFile;
         }

         const Token& Peek(size_t offset = 0) const
         {
            size_t idx = index + offset;
            if (idx < tokens.size()) return tokens[idx];
            return tokens.empty() ? sEof : tokens.back();
         }

         Token Advance()
         {
            if (!AtEnd()) return tokens[index++];
            return Peek();
         }

         bool MatchOp(const std::string& opStr)
         {
            if (!AtEnd() && Peek().IsOp(opStr))
            {
               Advance();
               return true;
            }
            return false;
         }

         bool MatchPunct(char c)
         {
            if (!AtEnd() && Peek().IsPunct(c))
            {
               Advance();
               return true;
            }
            return false;
         }

         void Fail(const std::string& msg, SourceSpan span = {})
         {
            if (error.Empty())
            {
               error.severity = Severity::Error;
               error.message = msg;
               error.span = (span.length > 0) ? span : (AtEnd() ? (!tokens.empty() ? tokens.back().span : SourceSpan{}) : Peek().span);
            }
         }

         bool Failed() const { return !error.Empty(); }

         static Token sEof;
      };

      Token ParserState::sEof = { TokenKind::EndOfFile, {}, 0.0, "" };

      AstNodePtr ParseOr(ParserState& p);

      AstNodePtr ParseAtom(ParserState& p)
      {
         if (p.Failed()) return nullptr;

         if (p.AtEnd())
         {
            p.Fail("unexpected trailing text");
            return nullptr;
         }

         const Token& tok = p.Peek();

         // Parenthesised expression
         if (tok.IsPunct('('))
         {
            SourceSpan startSpan = tok.span;
            p.Advance(); // consume '('
            AstNodePtr inner = ParseOr(p);
            if (!p.MatchPunct(')'))
            {
               p.Fail("expected ')'", startSpan);
               return nullptr;
            }
            return inner;
         }

         // Unary minus: '-'
         if (tok.IsOp("-"))
         {
            SourceSpan opSpan = tok.span;
            p.Advance();
            AstNodePtr operand = ParseAtom(p);
            if (!operand) return nullptr;
            return std::make_shared<AstUnary>("-", operand, opSpan);
         }

         // Unary plus: '+'
         if (tok.IsOp("+"))
         {
            p.Advance();
            return ParseAtom(p);
         }

         // Unary not: '!'
         if (tok.IsOp("!"))
         {
            SourceSpan opSpan = tok.span;
            p.Advance();
            AstNodePtr operand = ParseAtom(p);
            if (!operand) return nullptr;
            return std::make_shared<AstUnary>("!", operand, opSpan);
         }

         // Number literal
         if (tok.kind == TokenKind::Number)
         {
            Token numTok = p.Advance();
            return std::make_shared<AstLiteral>(numTok.numberValue, numTok.span);
         }

         // Boolean keyword literal: true / false
         if (tok.kind == TokenKind::Keyword && (tok.text == "true" || tok.text == "false"))
         {
            Token kwTok = p.Advance();
            return std::make_shared<AstLiteral>(kwTok.text == "true", kwTok.span);
         }

         // Identifier or function call
         if (tok.kind == TokenKind::Ident || tok.kind == TokenKind::Keyword)
         {
            Token idTok = p.Advance();
            // Function call lookahead: if next token is '('
            if (p.Peek().IsPunct('('))
            {
               SourceSpan callSpan = idTok.span;
               p.Advance(); // consume '('
               std::vector<AstNodePtr> args;
               if (!p.MatchPunct(')'))
               {
                  AstNodePtr firstArg = ParseOr(p);
                  if (firstArg) args.push_back(firstArg);
                  while (p.MatchPunct(','))
                  {
                     AstNodePtr nextArg = ParseOr(p);
                     if (nextArg) args.push_back(nextArg);
                  }
                  if (!p.MatchPunct(')'))
                  {
                     p.Fail("expected ')'", idTok.span);
                     return nullptr;
                  }
               }
               return std::make_shared<AstCall>(idTok.text, std::move(args), callSpan);
            }

            return std::make_shared<AstIdent>(idTok.text, idTok.span);
         }

         p.Fail("unexpected character", tok.span);
         return nullptr;
      }

      // Right-associative power operator: 2^3^2 == 2^(3^2)
      AstNodePtr ParsePower(ParserState& p)
      {
         AstNodePtr base = ParseAtom(p);
         if (p.Failed() || !base) return base;

         if (p.Peek().IsOp("^"))
         {
            Token opTok = p.Advance();
            AstNodePtr exp = ParsePower(p);
            if (!exp) return nullptr;
            return std::make_shared<AstBinary>("^", base, exp, opTok.span);
         }
         return base;
      }

      // Term: *, /, %
      AstNodePtr ParseTerm(ParserState& p)
      {
         AstNodePtr left = ParsePower(p);
         if (p.Failed() || !left) return left;

         while (!p.AtEnd() && (p.Peek().IsOp("*") || p.Peek().IsOp("/") || p.Peek().IsOp("%")))
         {
            Token opTok = p.Advance();
            AstNodePtr right = ParsePower(p);
            if (!right) return nullptr;
            left = std::make_shared<AstBinary>(opTok.text, left, right, opTok.span);
         }
         return left;
      }

      // Expr: +, -
      AstNodePtr ParseExpr(ParserState& p)
      {
         AstNodePtr left = ParseTerm(p);
         if (p.Failed() || !left) return left;

         while (!p.AtEnd() && (p.Peek().IsOp("+") || p.Peek().IsOp("-")))
         {
            Token opTok = p.Advance();
            AstNodePtr right = ParseTerm(p);
            if (!right) return nullptr;
            left = std::make_shared<AstBinary>(opTok.text, left, right, opTok.span);
         }
         return left;
      }

      // Comparisons: <, <=, >, >=, ==, !=
      AstNodePtr ParseCompare(ParserState& p)
      {
         AstNodePtr left = ParseExpr(p);
         if (p.Failed() || !left) return left;

         while (!p.AtEnd() && (p.Peek().IsOp("<") || p.Peek().IsOp("<=") ||
                               p.Peek().IsOp(">") || p.Peek().IsOp(">=") ||
                               p.Peek().IsOp("==") || p.Peek().IsOp("!=")))
         {
            Token opTok = p.Advance();
            AstNodePtr right = ParseExpr(p);
            if (!right) return nullptr;
            left = std::make_shared<AstBinary>(opTok.text, left, right, opTok.span);
         }
         return left;
      }

      // Logical AND: &&
      AstNodePtr ParseAnd(ParserState& p)
      {
         AstNodePtr left = ParseCompare(p);
         if (p.Failed() || !left) return left;

         while (!p.AtEnd() && p.Peek().IsOp("&&"))
         {
            Token opTok = p.Advance();
            AstNodePtr right = ParseCompare(p);
            if (!right) return nullptr;
            left = std::make_shared<AstBinary>("&&", left, right, opTok.span);
         }
         return left;
      }

      // Logical OR: ||
      AstNodePtr ParseOr(ParserState& p)
      {
         AstNodePtr left = ParseAnd(p);
         if (p.Failed() || !left) return left;

         while (!p.AtEnd() && p.Peek().IsOp("||"))
         {
            Token opTok = p.Advance();
            AstNodePtr right = ParseAnd(p);
            if (!right) return nullptr;
            left = std::make_shared<AstBinary>("||", left, right, opTok.span);
         }
         return left;
      }
   }

   bool ParseExpression(const std::vector<Token>& tokens, AstNodePtr& outExpr, FieldError& outError)
   {
      outError.Clear();
      outExpr = nullptr;

      // Filter non-significant tokens for single expression
      std::vector<Token> cleanTokens;
      for (const auto& t : tokens)
      {
         if (t.kind != TokenKind::Comment && t.kind != TokenKind::Newline)
         {
            cleanTokens.push_back(t);
         }
      }

      if (cleanTokens.empty() || cleanTokens.front().kind == TokenKind::EndOfFile)
      {
         outError.severity = Severity::Error;
         outError.message = "empty expression";
         return false;
      }

      ParserState state(cleanTokens, outError);
      outExpr = ParseOr(state);

      if (state.Failed() || !outExpr)
      {
         return false;
      }

      if (!state.AtEnd())
      {
         outError.severity = Severity::Error;
         outError.message = "unexpected trailing text";
         outError.span = state.Peek().span;
         return false;
      }

      return true;
   }

   bool ParseProgram(const std::vector<Token>& tokens, AstNodePtr& outProgram, FieldError& outError)
   {
      // In step 1, a program is parsed as a single expression wrap into AstProgram
      AstNodePtr singleExpr;
      if (!ParseExpression(tokens, singleExpr, outError))
      {
         return false;
      }
      outProgram = std::make_shared<AstProgram>(std::vector<AstNodePtr>{ singleExpr });
      return true;
   }
}
