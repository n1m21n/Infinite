#pragma once

#include "FieldAst.h"
#include "FieldLex.h"
#include <vector>

namespace Field
{
   // Parses a single expression from tokens
   bool ParseExpression(const std::vector<Token>& tokens, AstNodePtr& outExpr, FieldError& outError);

   // Parses a full Field program / kernel body with statements and declarations
   bool ParseProgram(const std::vector<Token>& tokens, AstNodePtr& outProgram, FieldError& outError);
}
