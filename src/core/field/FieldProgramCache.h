#pragma once

#include "FieldBytecode.h"
#include "FieldError.h"
#include "FieldVM.h"
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Field
{
   struct CachedEntry
   {
      bool success = false;
      BytecodeProgram program;
      std::string compileError;
   };

   class FieldProgramCache
   {
   public:
      static FieldProgramCache& Instance();

      // Gets or compiles an expression string into bytecode
      bool GetOrCompile(const std::string& text,
                        const std::map<std::string, float>* siblings,
                        const std::map<std::string, float>* globals,
                        BytecodeProgram& outProg,
                        std::string& outCompileError);

      void Clear();

   private:
      FieldProgramCache() = default;

      std::string MakeKey(const std::string& text,
                          const std::map<std::string, float>* siblings,
                          const std::map<std::string, float>* globals);

      std::mutex mMutex;
      static constexpr size_t kMaxCacheEntries = 1024;
      std::list<std::string> mOrder;
      std::unordered_map<std::string, std::pair<CachedEntry, std::list<std::string>::iterator>> mCache;
   };
}
