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

         void SkipSeparators()
         {
            while (!AtEnd() && (Peek().kind == TokenKind::Newline || Peek().kind == TokenKind::Comment || Peek().IsPunct(';')))
            {
               Advance();
            }
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
      AstNodePtr ParseStatement(ParserState& p);
      AstNodePtr ParseBlock(ParserState& p);

      AstNodePtr ParsePrimary(ParserState& p)
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
            AstNodePtr operand = ParsePrimary(p);
            if (!operand) return nullptr;
            return std::make_shared<AstUnary>("-", operand, opSpan);
         }

         // Unary plus: '+'
         if (tok.IsOp("+"))
         {
            p.Advance();
            return ParsePrimary(p);
         }

         // Unary not: '!'
         if (tok.IsOp("!"))
         {
            SourceSpan opSpan = tok.span;
            p.Advance();
            AstNodePtr operand = ParsePrimary(p);
            if (!operand) return nullptr;
            return std::make_shared<AstUnary>("!", operand, opSpan);
         }

         // Number literal
         if (tok.kind == TokenKind::Number)
         {
            Token numTok = p.Advance();
            return std::make_shared<AstLiteral>(numTok.numberValue, numTok.span);
         }

         // String literal (D7, step 10): only legal as emit()'s first arg or
         // set()'s second arg - checked during graph-program lowering.
         if (tok.kind == TokenKind::String)
         {
            Token strTok = p.Advance();
            return std::make_shared<AstLiteral>(strTok.text, strTok.span, true);
         }

         // Boolean keyword literal: true / false
         if (tok.kind == TokenKind::Keyword && (tok.text == "true" || tok.text == "false"))
         {
            Token kwTok = p.Advance();
            return std::make_shared<AstLiteral>(kwTok.text == "true", kwTok.span);
         }

         // Identifier or function/constructor call
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

      // Postfix access (swizzles / dotted calls like reduce.sum / bracket indexing): Primary ( ( '.' ( SWIZZLE | METHOD(...) ) ) | '[' EXPR ']' )*
      AstNodePtr ParsePostfix(ParserState& p)
      {
         AstNodePtr base = ParsePrimary(p);
         if (p.Failed() || !base) return base;

         while (!p.AtEnd())
         {
            if (p.Peek().IsOp("."))
            {
               Token dotTok = p.Advance(); // consume '.'
               if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
               {
                  p.Fail("expected a component name after '.'", dotTok.span);
                  return nullptr;
               }
               Token fieldTok = p.Advance();

               if (!p.AtEnd() && p.Peek().IsPunct('('))
               {
                  Token lparen = p.Advance(); // consume '('
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
                        p.Fail("expected ')'", lparen.span);
                        return nullptr;
                     }
                  }

                  std::string calleeName;
                  if (base->kind == AstKind::Ident)
                  {
                     calleeName = std::static_pointer_cast<AstIdent>(base)->name + "." + fieldTok.text;
                  }
                  else
                  {
                     calleeName = fieldTok.text;
                  }

                  SourceSpan span = { base->span.offset, base->span.line, base->span.col,
                                      (fieldTok.span.offset + fieldTok.span.length) - base->span.offset };
                  base = std::make_shared<AstCall>(calleeName, std::move(args), span);
               }
               else
               {
                  SourceSpan span = { base->span.offset, base->span.line, base->span.col,
                                      (fieldTok.span.offset + fieldTok.span.length) - base->span.offset };
                  base = std::make_shared<AstAccess>(base, fieldTok.text, span);
               }
            }
            else if (p.Peek().IsPunct('['))
            {
               // Do not consume '[' for index access if base is a literal (e.g. `state float A = 1 [wrap]`)
               if (base->kind != AstKind::Ident && base->kind != AstKind::Access &&
                   base->kind != AstKind::Call && base->kind != AstKind::Index)
               {
                  break;
               }
               Token lbracket = p.Advance(); // consume '['
               AstNodePtr indexExpr = ParseOr(p);
               if (p.Failed() || !indexExpr) return nullptr;
               if (!p.Peek().IsPunct(']'))
               {
                  p.Fail("expected ']' after index expression", lbracket.span);
                  return nullptr;
               }
               Token rbracket = p.Advance(); // consume ']'
               SourceSpan span = { base->span.offset, base->span.line, base->span.col,
                                   (rbracket.span.offset + rbracket.span.length) - base->span.offset };
               base = std::make_shared<AstIndex>(base, indexExpr, span);
            }
            else
            {
               break;
            }
         }

         return base;
      }

      AstNodePtr ParseAtom(ParserState& p)
      {
         return ParsePostfix(p);
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

      bool IsTypeKeyword(const std::string& s)
      {
         return s == "float" || s == "int" || s == "bool" ||
                s == "vec2" || s == "vec3" || s == "vec4";
      }

      // Build step 12: output/input declarations accept the ordinary scalar
      // type keywords plus three reserved structural type-names that are not
      // registered as lexer keywords (they remain plain identifiers
      // everywhere else) - geometry (input-only, whole mesh), audio (an
      // alias for a sample-domain float), image (an alias for a pixel-domain
      // vec4). See docs/plans/field/step-12-dynamic-pins-ir.md S1.5/S5.6.
      bool IsPinTypeToken(const Token& tok)
      {
         if (IsTypeKeyword(tok.text)) return true;
         if (tok.kind != TokenKind::Ident) return false;
         return tok.text == "geometry" || tok.text == "audio" || tok.text == "image";
      }

      bool IsDomainToken(const Token& tok)
      {
         if (tok.kind != TokenKind::Ident) return false;
         return tok.text == "graph" || tok.text == "frame" || tok.text == "element" ||
                tok.text == "pixel" || tok.text == "sample";
      }

      bool ParseLiteralNumber(ParserState& p, double& outVal, SourceSpan& outSpan)
      {
         if (p.AtEnd()) return false;
         double sign = 1.0;
         outSpan = p.Peek().span;
         if (p.Peek().IsOp("-"))
         {
            p.Advance();
            sign = -1.0;
         }
         else if (p.Peek().IsOp("+"))
         {
            p.Advance();
         }

         if (p.AtEnd() || p.Peek().kind != TokenKind::Number)
         {
            return false;
         }

         Token numTok = p.Advance();
         outVal = sign * numTok.numberValue;
         outSpan.length = (numTok.span.offset + numTok.span.length) - outSpan.offset;
         return true;
      }

      AstNodePtr ParseParamDecl(ParserState& p)
      {
         Token paramTok = p.Advance(); // consume 'param'
         if (p.AtEnd())
         {
            p.Fail("type is required in param declaration", paramTok.span);
            return nullptr;
         }

         const Token& typeTok = p.Peek();
         if (!IsTypeKeyword(typeTok.text))
         {
            p.Fail("type is required in param declaration", typeTok.span);
            return nullptr;
         }
         p.Advance(); // consume type
         if (typeTok.text != "float")
         {
            p.Fail("float params only in v1", typeTok.span);
            return nullptr;
         }

         if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
         {
            p.Fail("expected param name", typeTok.span);
            return nullptr;
         }
         Token nameTok = p.Advance();

         if (!p.MatchOp("="))
         {
            p.Fail("expected '=' after param name", nameTok.span);
            return nullptr;
         }

         double defVal = 0.0;
         SourceSpan defSpan;
         if (!ParseLiteralNumber(p, defVal, defSpan))
         {
            p.Fail("param initial value must be a literal number", p.Peek().span);
            return nullptr;
         }

         if (!p.MatchPunct('['))
         {
            p.Fail("expected range '[min, max]' in param declaration", p.Peek().span);
            return nullptr;
         }

         double minVal = 0.0;
         SourceSpan minSpan;
         if (!ParseLiteralNumber(p, minVal, minSpan))
         {
            p.Fail("range bounds must be literal numbers", p.Peek().span);
            return nullptr;
         }

         if (!p.MatchPunct(','))
         {
            p.Fail("expected ',' between min and max in param range", p.Peek().span);
            return nullptr;
         }

         double maxVal = 0.0;
         SourceSpan maxSpan;
         if (!ParseLiteralNumber(p, maxVal, maxSpan))
         {
            p.Fail("range bounds must be literal numbers", p.Peek().span);
            return nullptr;
         }

         if (!p.MatchPunct(']'))
         {
            p.Fail("range bounds must be literal numbers (expected ']')", p.Peek().span);
            return nullptr;
         }

         if (minVal > maxVal)
         {
            p.Fail("min must be <= max in param range", paramTok.span);
            return nullptr;
         }

         return std::make_shared<AstDeclParam>("float", nameTok.text, defVal, minVal, maxVal, nullptr, paramTok.span);
      }

      bool IsLiteralConstant(const AstNodePtr& node, std::string& outNonConst)
      {
         if (!node) return false;
         if (node->kind == AstKind::Literal)
         {
            return true;
         }
         if (node->kind == AstKind::Unary)
         {
            auto un = std::static_pointer_cast<AstUnary>(node);
            if (un->op == "-" || un->op == "+" || un->op == "!")
            {
               return IsLiteralConstant(un->operand, outNonConst);
            }
         }
         if (node->kind == AstKind::Call)
         {
            auto call = std::static_pointer_cast<AstCall>(node);
            if (call->callee == "vec2" || call->callee == "vec3" || call->callee == "vec4")
            {
               for (const auto& arg : call->args)
               {
                  if (!IsLiteralConstant(arg, outNonConst))
                     return false;
               }
               return true;
            }
         }
         if (node->kind == AstKind::Ident)
         {
            auto id = std::static_pointer_cast<AstIdent>(node);
            outNonConst = id->name;
            return false;
         }
         if (node->kind == AstKind::Binary)
         {
            auto bin = std::static_pointer_cast<AstBinary>(node);
            outNonConst = bin->op;
            return false;
         }
         outNonConst = "expression";
         return false;
      }

      AstNodePtr ParseStateDecl(ParserState& p)
      {
         Token stateTok = p.Advance(); // consume 'state'
         if (p.AtEnd())
         {
            p.Fail("a type is required in state declaration", stateTok.span);
            return nullptr;
         }

         const Token& typeTok = p.Peek();
         if (!IsTypeKeyword(typeTok.text))
         {
            p.Fail("a type is required in state declaration", typeTok.span);
            return nullptr;
         }
         p.Advance(); // consume type

         if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
         {
            p.Fail("expected state cell name", typeTok.span);
            return nullptr;
         }
         Token nameTok = p.Advance();
         std::string name = nameTok.text;

         // Check reserved words per domain
         if (name == "t" || name == "dt" || name == "frame" || name == "count")
         {
            p.Fail("state cell '" + name + "' cannot shadow reserved attribute of frame domain", nameTok.span);
            return nullptr;
         }
         if (name == "P" || name == "N" || name == "uv" || name == "Cd" || name == "i" || name == "n")
         {
            p.Fail("state cell '" + name + "' cannot shadow reserved attribute of element domain", nameTok.span);
            return nullptr;
         }
         if (name == "in" || name == "out" || name == "sr" || name == "freq" || name == "gate")
         {
            p.Fail("state cell '" + name + "' cannot shadow reserved attribute of sample domain", nameTok.span);
            return nullptr;
         }
         if (name == "xy" || name == "col" || name == "res")
         {
            p.Fail("state cell '" + name + "' cannot shadow reserved attribute of pixel domain", nameTok.span);
            return nullptr;
         }
         // `age` is reserved in BOTH the pixel (step 22) and element (step 23)
         // domains - it is the one-shot seeding signal for every simulation.
         if (name == "age")
         {
            p.Fail("state cell '" + name + "' cannot shadow the reserved attribute 'age'", nameTok.span);
            return nullptr;
         }
         if (name == "pi" || name == "lo" || name == "hi")
         {
            p.Fail("state cell '" + name + "' cannot shadow reserved identifier", nameTok.span);
            return nullptr;
         }

         int tableSize = 0;
         if (p.MatchPunct('['))
         {
            if (p.AtEnd())
            {
               p.Fail("expected integer literal for table size in state declaration", nameTok.span);
               return nullptr;
            }
            Token sizeTok = p.Peek();
            if (sizeTok.kind != TokenKind::Number)
            {
               std::string nonConst = (sizeTok.kind == TokenKind::Ident) ? sizeTok.text : "expression";
               p.Fail("table size must be a compile-time constant integer literal (non-constant: '" + nonConst + "')", sizeTok.span);
               return nullptr;
            }
            p.Advance();
            double numVal = sizeTok.numberValue;
            if (numVal < 1.0 || floor(numVal) != numVal)
            {
               p.Fail("table size must be a positive integer literal >= 1", sizeTok.span);
               return nullptr;
            }
            tableSize = (int)numVal;
            if (!p.MatchPunct(']'))
            {
               p.Fail("expected ']' after table size in state declaration", sizeTok.span);
               return nullptr;
            }
            if (typeTok.text != "float")
            {
               p.Fail("table state cell '" + name + "' must have element type 'float' in v1", typeTok.span);
               return nullptr;
            }
         }

         if (!p.MatchOp("="))
         {
            p.Fail("expected '=' in state declaration", nameTok.span);
            return nullptr;
         }

         AstNodePtr initExpr = ParseOr(p);
         if (p.Failed() || !initExpr) return nullptr;

         std::string nonConst;
         if (!IsLiteralConstant(initExpr, nonConst))
         {
            p.Fail("state initial value must be a literal constant (non-constant: '" + nonConst + "')", initExpr->span);
            return nullptr;
         }

         // Build step 22 (OPEN-C): optional boundary mode, e.g.
         //   state float A = 1 [wrap]
         // It decides what an offset read A(uv + d) returns outside [0,1].
         // Absent means clamp, which is what every shipping Feedback/Trails
         // node already does.
         std::string boundary;
         if (p.MatchPunct('['))
         {
            if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
            {
               p.Fail("expected a boundary mode (clamp, wrap or border) after '[' in state declaration", nameTok.span);
               return nullptr;
            }
            Token bndTok = p.Advance();
            if (bndTok.text != "clamp" && bndTok.text != "wrap" && bndTok.text != "border")
            {
               p.Fail("unknown boundary mode '" + bndTok.text + "' (expected clamp, wrap or border)", bndTok.span);
               return nullptr;
            }
            boundary = bndTok.text;
            if (!p.MatchPunct(']'))
            {
               p.Fail("expected ']' after boundary mode '" + boundary + "'", bndTok.span);
               return nullptr;
            }
         }

         return std::make_shared<AstDeclState>(typeTok.text, nameTok.text, initExpr, stateTok.span, boundary, tableSize);
      }

      AstNodePtr ParseAttribDecl(ParserState& p)
      {
         Token attribTok = p.Advance(); // consume 'attrib'
         if (p.AtEnd())
         {
            p.Fail("expected type name in attribute declaration", attribTok.span);
            return nullptr;
         }

         const Token& typeTok = p.Peek();
         if (!IsTypeKeyword(typeTok.text))
         {
            p.Fail("type is required in attribute declaration", typeTok.span);
            return nullptr;
         }
         p.Advance(); // consume type

         if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
         {
            p.Fail("expected attribute name", typeTok.span);
            return nullptr;
         }
         Token nameTok = p.Advance();

         AstNodePtr initExpr = nullptr;
         if (p.MatchOp("="))
         {
            initExpr = ParseOr(p);
            if (p.Failed() || !initExpr) return nullptr;
         }

         return std::make_shared<AstDeclAttrib>(typeTok.text, nameTok.text, initExpr, attribTok.span);
      }

      // Build step 12: `output <domain> <type> <name> = <expr>`
      AstNodePtr ParseOutputDecl(ParserState& p)
      {
         Token outputTok = p.Advance(); // consume 'output'
         if (p.AtEnd() || !IsDomainToken(p.Peek()))
         {
            p.Fail("expected a domain name (graph, frame, element, pixel, sample) after 'output'", p.Peek().span);
            return nullptr;
         }
         Token domainTok = p.Advance();

         if (p.AtEnd() || !IsPinTypeToken(p.Peek()))
         {
            p.Fail("expected a type after 'output " + domainTok.text + "'", p.Peek().span);
            return nullptr;
         }
         Token typeTok = p.Advance();

         if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
         {
            p.Fail("expected output pin name", typeTok.span);
            return nullptr;
         }
         Token nameTok = p.Advance();

         if (!p.MatchOp("="))
         {
            p.Fail("expected '=' after output pin name", nameTok.span);
            return nullptr;
         }

         AstNodePtr initExpr = ParseOr(p);
         if (p.Failed() || !initExpr) return nullptr;

         return std::make_shared<AstDeclOutput>(domainTok.text, typeTok.text, nameTok.text, initExpr, outputTok.span);
      }

      // Build step 12: `input <domain> <type> <name>`
      AstNodePtr ParseInputDecl(ParserState& p)
      {
         Token inputTok = p.Advance(); // consume 'input'
         if (p.AtEnd() || !IsDomainToken(p.Peek()))
         {
            p.Fail("expected a domain name (graph, frame, element, pixel, sample) after 'input'", p.Peek().span);
            return nullptr;
         }
         Token domainTok = p.Advance();

         if (p.AtEnd() || !IsPinTypeToken(p.Peek()))
         {
            p.Fail("expected a type after 'input " + domainTok.text + "'", p.Peek().span);
            return nullptr;
         }
         Token typeTok = p.Advance();

         if (p.AtEnd() || (p.Peek().kind != TokenKind::Ident && p.Peek().kind != TokenKind::Keyword))
         {
            p.Fail("expected input pin name", typeTok.span);
            return nullptr;
         }
         Token nameTok = p.Advance();

         return std::make_shared<AstDeclInput>(domainTok.text, typeTok.text, nameTok.text, inputTok.span);
      }

      AstNodePtr ParseIf(ParserState& p)
      {
         Token ifTok = p.Advance(); // consume 'if'
         if (!p.MatchPunct('('))
         {
            p.Fail("expected '(' after 'if'", ifTok.span);
            return nullptr;
         }

         AstNodePtr cond = ParseOr(p);
         if (p.Failed() || !cond) return nullptr;

         if (!p.MatchPunct(')'))
         {
            p.Fail("expected ')' after if condition", ifTok.span);
            return nullptr;
         }

         p.SkipSeparators();
         AstNodePtr thenBlock = ParseStatement(p);
         if (p.Failed() || !thenBlock) return nullptr;

         AstNodePtr elseBlock = nullptr;
         p.SkipSeparators();
         if (!p.AtEnd() && p.Peek().kind == TokenKind::Keyword && p.Peek().text == "else")
         {
            p.Advance(); // consume 'else'
            p.SkipSeparators();
            elseBlock = ParseStatement(p);
            if (p.Failed() || !elseBlock) return nullptr;
         }

         return std::make_shared<AstIf>(cond, thenBlock, elseBlock, ifTok.span);
      }

      AstNodePtr ParseFor(ParserState& p)
      {
         Token forTok = p.Advance(); // consume 'for'
         if (!p.MatchPunct('('))
         {
            p.Fail("expected '(' after 'for'", forTok.span);
            return nullptr;
         }

         AstNodePtr init = nullptr;
         if (!p.Peek().IsPunct(';'))
         {
            init = ParseStatement(p);
            if (p.Failed()) return nullptr;
         }
         p.MatchPunct(';');

         AstNodePtr cond = nullptr;
         if (!p.Peek().IsPunct(';'))
         {
            cond = ParseOr(p);
            if (p.Failed()) return nullptr;
         }
         p.MatchPunct(';');

         AstNodePtr step = nullptr;
         if (!p.Peek().IsPunct(')'))
         {
            step = ParseStatement(p);
            if (p.Failed()) return nullptr;
         }

         if (!p.MatchPunct(')'))
         {
            p.Fail("expected ')' in for loop header", forTok.span);
            return nullptr;
         }

         p.SkipSeparators();
         AstNodePtr body = ParseStatement(p);
         if (p.Failed() || !body) return nullptr;

         return std::make_shared<AstFor>(init, cond, step, body, forTok.span);
      }

      AstNodePtr ParseBlock(ParserState& p)
      {
         Token lbrace = p.Advance(); // consume '{'
         std::vector<AstNodePtr> stmts;

         p.SkipSeparators();
         while (!p.AtEnd() && !p.Peek().IsPunct('}'))
         {
            AstNodePtr stmt = ParseStatement(p);
            if (p.Failed() || !stmt) return nullptr;
            stmts.push_back(stmt);
            p.SkipSeparators();
         }

         if (!p.MatchPunct('}'))
         {
            p.Fail("expected '}'", lbrace.span);
            return nullptr;
         }

         return std::make_shared<AstBlock>(std::move(stmts), lbrace.span);
      }

      AstNodePtr ParseAssignOrExpr(ParserState& p)
      {
         AstNodePtr lhs = ParseOr(p);
         if (p.Failed() || !lhs) return lhs;

         if (!p.AtEnd() && (p.Peek().IsOp("=") || p.Peek().IsOp("+=") || p.Peek().IsOp("-=") ||
                            p.Peek().IsOp("*=") || p.Peek().IsOp("/=")))
         {
            Token opTok = p.Advance();
            AstNodePtr rhs = ParseOr(p);
            if (p.Failed() || !rhs) return nullptr;
            return std::make_shared<AstAssign>(opTok.text, lhs, rhs, opTok.span);
         }

         if (!p.AtEnd() && (p.Peek().IsOp("++") || p.Peek().IsOp("--")))
         {
            Token opTok = p.Advance();
            std::string op = (opTok.text == "++") ? "+=" : "-=";
            auto oneLit = std::make_shared<AstLiteral>(1.0, opTok.span);
            return std::make_shared<AstAssign>(op, lhs, oneLit, opTok.span);
         }

         return lhs;
      }

      AstNodePtr ParseStatement(ParserState& p)
      {
         if (p.Failed() || p.AtEnd()) return nullptr;

         const Token& tok = p.Peek();

         if (tok.IsPunct('{'))
         {
            return ParseBlock(p);
         }

         if (tok.kind == TokenKind::Keyword)
         {
            if (tok.text == "attrib")
            {
               return ParseAttribDecl(p);
            }
            if (tok.text == "state")
            {
               return ParseStateDecl(p);
            }
            if (tok.text == "param")
            {
               return ParseParamDecl(p);
            }
            if (tok.text == "output")
            {
               return ParseOutputDecl(p);
            }
            if (tok.text == "input")
            {
               return ParseInputDecl(p);
            }
            if (tok.text == "if")
            {
               return ParseIf(p);
            }
            if (tok.text == "for")
            {
               return ParseFor(p);
            }
            if (tok.text == "map")
            {
               Token mapTok = p.Advance(); // consume 'map'
               AstNodePtr countExpr = nullptr;
               if (!p.AtEnd() && p.Peek().IsPunct('('))
               {
                  p.Advance(); // consume '('
                  countExpr = ParseOr(p);
                  if (!p.MatchPunct(')'))
                  {
                     p.Fail("expected ')' after map count expression", mapTok.span);
                     return nullptr;
                  }
               }
               p.SkipSeparators();
               AstNodePtr body = ParseStatement(p);
               if (p.Failed() || !body) return nullptr;
               return std::make_shared<AstMap>(countExpr, body, mapTok.span);
            }
         }

         return ParseAssignOrExpr(p);
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
      outError.Clear();
      outProgram = nullptr;

      std::vector<Token> filteredTokens;
      for (const auto& t : tokens)
      {
         if (t.kind != TokenKind::Comment)
         {
            filteredTokens.push_back(t);
         }
      }

      if (filteredTokens.empty() || filteredTokens.front().kind == TokenKind::EndOfFile)
      {
         outProgram = std::make_shared<AstProgram>();
         return true;
      }

      ParserState state(filteredTokens, outError);
      std::vector<AstNodePtr> statements;

      state.SkipSeparators();
      while (!state.AtEnd())
      {
         AstNodePtr stmt = ParseStatement(state);
         if (state.Failed() || !stmt)
         {
            return false;
         }
         statements.push_back(stmt);
         state.SkipSeparators();
      }

      outProgram = std::make_shared<AstProgram>(std::move(statements));
      return true;
   }
}
