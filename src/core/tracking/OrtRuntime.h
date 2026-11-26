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

      // Where the pack keeps this platform's runtime: <packDir>/lib/ plus
      // libonnxruntime.dylib, onnxruntime.dll or libonnxruntime.so.
      static std::string PackLibraryPath(const std::string& packDir);
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

      // Named inputs, float or int64; outputs come back as float in model
      // output order (float16 and uint8 outputs are widened). Used by the selection
      // models, whose decoders take several inputs.
      struct Input
      {
         std::string name;
         std::vector<int64_t> shape;
         const float* f32 = nullptr;   // one of f32 / i64
         const int64_t* i64 = nullptr;
         const uint8_t* u8 = nullptr;  // image models that take 8-bit pixels (MI-GAN)
      };
      bool RunNamed(OrtModel* m, const std::vector<Input>& ins, std::vector<Tensor>& outs, std::string& err);
      // Input names of a model, in order.
      std::vector<std::string> InputNames(OrtModel* m) const;

   private:
      void* m_lib = nullptr;
      const void* m_api = nullptr;  // const OrtApi*
      void* m_env = nullptr;
      void* m_gpuFn = nullptr;      // OrtSessionOptionsAppendExecutionProvider_CoreML
   };
}
