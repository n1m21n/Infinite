#pragma once
// Built-in OpenAI-compatible custom agent (Ollama, DeepSeek, OpenRouter, custom base URL).
// Streams /chat/completions, routes graph edits through the `propose` RPC gate, caps at 12 steps/turn,
// and falls back to plain chat without tools when a server rejects tool definitions.

#include "core/chat/AgentStream.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

namespace chat
{
   struct CustomPreset
   {
      const char* id;
      const char* name;
      const char* baseUrl;
      const char* defaultModel;
   };

   const std::vector<CustomPreset>& CustomPresets();
   const CustomPreset* FindCustomPreset(const std::string& id);
   std::string NormalizeOpenAIBaseUrl(const std::string& baseUrl);

   // OpenAI tool definitions matching the Infinite MCP/RPC surface (all edits via `propose`).
   nlohmann::json OpenAIAgentTools();

   // Incremental SSE parser for OpenAI /chat/completions streaming responses.
   struct OpenAIToolCall
   {
      std::string id;
      std::string name;
      std::string arguments;
   };

   class OpenAIStreamParser
   {
   public:
      void Feed(const std::string& chunk);
      void Finish();

      const std::string& Text() const { return mText; }
      const std::vector<OpenAIToolCall>& ToolCalls() const { return mToolCalls; }
      const TurnUsage& Usage() const { return mUsage; }
      bool SawDone() const { return mDone; }

   private:
      void ProcessLine(const std::string& line);
      std::string mBuffer;
      std::string mText;
      std::vector<OpenAIToolCall> mToolCalls;
      TurnUsage mUsage;
      bool mDone = false;
   };

   // Returns true if an HTTP 400/422 error response indicates the endpoint/model rejected `tools`.
   bool IsToolsUnsupportedError(int status, const std::string& errorBody);

   // Synchronous connection test (run on a worker thread from UI).
   bool TestOpenAIConnection(const std::string& baseUrl, const std::string& model,
                             const std::string& apiKey, std::string& outMessage);

   class OpenAIAgentRun
   {
   public:
      using ToolExecutor = std::function<bool(const std::string& method, const nlohmann::json& params,
                                              nlohmann::json& outResult, std::string& outError)>;

      struct Options
      {
         std::string baseUrl;
         std::string model;
         std::string apiKey;
         std::string systemPrompt;
         std::vector<std::pair<std::string, std::string>> history; // (role, content)
         std::string userPrompt;
         int maxSteps = 12;
      };

      static std::shared_ptr<OpenAIAgentRun> Start(const Options& options);
      ~OpenAIAgentRun();

      // Called on the main thread each frame: executes any pending tool call via `exec` and drains events.
      void Poll(std::vector<AgentEvent>& out, const ToolExecutor& exec);
      void Stop();
      bool Done() const { return mDone.load(); }

   private:
      explicit OpenAIAgentRun(Options options);
      void RunThread();
      nlohmann::json ExecuteToolOnMain(const std::string& name, const std::string& argsJson);
      void PushEvent(AgentEvent ev);

      Options mOptions;
      std::thread mThread;
      std::atomic<bool> mStop{ false };
      std::atomic<bool> mDone{ false };

      std::mutex mEventMutex;
      std::vector<AgentEvent> mEvents;

      struct PendingTool
      {
         bool active = false;
         bool completed = false;
         std::string method;
         nlohmann::json params;
         nlohmann::json result;
         std::string error;
         bool ok = false;
      };
      std::mutex mToolMutex;
      std::condition_variable mToolCv;
      PendingTool mPendingTool;
   };
}
