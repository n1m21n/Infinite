#pragma once

// ONNX Runtime loaded at run time from an extension pack (no link-time
// dependency): the pack ships the official libonnxruntime and the app talks to
// it through the C API only. If the library is missing, Load() fails and the
// tracking nodes show their Install hint.

#include <cstdint>
#include <string>
#include <vector>

namespace Tracking
{
   struct OrtModel;

   class OrtRuntime
   {
   public:
      ~OrtRuntime();

      // libPath: full path of libonnxruntime (.dylib/.so/.dll) inside the pack.
      bool Load(const std::string& libPath, std::string& err);
      bool Loaded() const { return m_api != nullptr; }

      // preferGpu: Core ML on macOS (no-op elsewhere for now). usedGpu reports
      // whether the provider registered.
      OrtModel* OpenModel(const std::string& onnxPath, bool preferGpu, bool& usedGpu, std::string& err);
      void CloseModel(OrtModel* m);

      struct Tensor
      {
         std::vector<int64_t> shape;
         std::vector<float> data;
      };
      // One float input (NHWC), all outputs returned in model output order.
      bool Run(OrtModel* m, const float* input, const std::vector<int64_t>& inShape,
               std::vector<Tensor>& outs, std::vector<std::string>& outNames, std::string& err);

   private:
      void* m_lib = nullptr;
      const void* m_api = nullptr;  // const OrtApi*
      void* m_env = nullptr;
      void* m_gpuFn = nullptr;      // OrtSessionOptionsAppendExecutionProvider_CoreML
   };
}
