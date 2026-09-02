#include "FieldProgramCache.h"
#include "FieldLex.h"
#include "FieldParse.h"
#include "FieldIR.h"

#include <sstream>

namespace Field
{
   FieldProgramCache& FieldProgramCache::Instance()
   {
      static FieldProgramCache sInstance;
      return sInstance;
   }

   std::string FieldProgramCache::MakeKey(const std::string& text,
                                          const std::map<std::string, float>* siblings,
                                          const std::map<std::string, float>* globals)
   {
      std::string key = text;
      key += "||";
      if (siblings)
      {
         for (const auto& kv : *siblings)
         {
            key += kv.first;
            key += ",";
         }
      }
      key += "||";
      if (globals)
      {
         for (const auto& kv : *globals)
         {
            key += kv.first;
            key += ",";
         }
      }
      return key;
   }

   bool FieldProgramCache::GetOrCompile(const std::string& text,
                                        const std::map<std::string, float>* siblings,
                                        const std::map<std::string, float>* globals,
                                        BytecodeProgram& outProg,
                                        std::string& outCompileError)
   {
      std::string key = MakeKey(text, siblings, globals);

      std::lock_guard<std::mutex> lock(mMutex);
      auto it = mCache.find(key);
      if (it != mCache.end())
      {
         // Move key to front of LRU list
         mOrder.erase(it->second.second);
         mOrder.push_front(key);
         it->second.second = mOrder.begin();

         const CachedEntry& entry = it->second.first;
         if (entry.success)
         {
            outProg = entry.program;
            outCompileError.clear();
            return true;
         }
         else
         {
            outCompileError = entry.compileError;
            return false;
         }
      }

      // Compile pipeline: Lex -> Parse -> Infer -> Bytecode
      std::vector<Token> tokens;
      FieldError err;

      CachedEntry newEntry;

      if (!Lex(text, tokens, err))
      {
         newEntry.success = false;
         newEntry.compileError = err.message;
      }
      else
      {
         AstNodePtr ast;
         if (!ParseExpression(tokens, ast, err))
         {
            newEntry.success = false;
            newEntry.compileError = err.message;
         }
         else
         {
            IRNodePtr ir;
            if (!LowerAstToIR(ast, ir, err))
            {
               newEntry.success = false;
               newEntry.compileError = err.message;
            }
            else
            {
               BytecodeProgram prog;
               if (!EmitBytecode(ir, prog, err))
               {
                  newEntry.success = false;
                  newEntry.compileError = err.message;
               }
               else
               {
                  newEntry.success = true;
                  newEntry.program = prog;
               }
            }
         }
      }

      // Enforce LRU capacity limit
      if (mCache.size() >= kMaxCacheEntries)
      {
         std::string oldestKey = mOrder.back();
         mOrder.pop_back();
         mCache.erase(oldestKey);
      }

      mOrder.push_front(key);
      mCache[key] = { newEntry, mOrder.begin() };

      if (newEntry.success)
      {
         outProg = newEntry.program;
         outCompileError.clear();
         return true;
      }
      else
      {
         outCompileError = newEntry.compileError;
         return false;
      }
   }

   void FieldProgramCache::Clear()
   {
      std::lock_guard<std::mutex> lock(mMutex);
      mCache.clear();
      mOrder.clear();
   }
}
