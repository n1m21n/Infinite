#include "OrtRuntime.h"

#include "onnxruntime_c_api.h"
#include "platform/AppPaths.h"

#include <cstring>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace Tracking
{
   struct OrtModel
   {
      OrtSession* session = nullptr;
      std::string inName;
      std::vector<std::string> inNames;
      std::vector<std::string> outNames;
   };

   static const OrtApi* A(const void* p) { return static_cast<const OrtApi*>(p); }

   static bool Check(const OrtApi* api, OrtStatus* st, std::string& err)
   {
      if (!st) return true;
      err = api->GetErrorMessage(st);
      api->ReleaseStatus(st);
      return false;
   }

   OrtRuntime::~OrtRuntime()
   {
      if (m_env && m_api) A(m_api)->ReleaseEnv(static_cast<OrtEnv*>(m_env));
      // The library stays mapped: ORT does not support unload.
   }

   std::string OrtRuntime::PackLibraryPath(const std::string& packDir)
   {
#if defined(__APPLE__)
      return packDir + "/lib/libonnxruntime.dylib";
#elif defined(_WIN32)
      return packDir + "/lib/onnxruntime.dll";
#else
      return packDir + "/lib/libonnxruntime.so";
#endif
   }

   bool OrtRuntime::Load(const std::string& libPath, std::string& err)
   {
      if (m_api) return true;
#if defined(_WIN32)
      // Wide path (non-ASCII profile folders) and altered search so the DLL's neighbours resolve.
      m_lib = reinterpret_cast<void*>(LoadLibraryExW(AppPaths::FsPath(libPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
#else
      m_lib = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
      if (!m_lib)
      {
#if defined(_WIN32)
         err = "cannot load " + libPath;
#else
         const char* e = dlerror();
         err = e ? e : ("cannot load " + libPath);
#endif
         return false;
      }
#if defined(_WIN32)
      auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_lib), n)); };
#else
      auto sym = [&](const char* n) { return dlsym(m_lib, n); };
#endif
      using GetBaseFn = const OrtApiBase* (*)();
      auto getBase = reinterpret_cast<GetBaseFn>(sym("OrtGetApiBase"));
      if (!getBase) { err = "OrtGetApiBase not found"; return false; }
      const OrtApi* api = getBase()->GetApi(ORT_API_VERSION);
      if (!api) { err = "runtime is older than the API this app was built for"; return false; }
      OrtEnv* env = nullptr;
      if (!Check(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "infinite-tracking", &env), err)) return false;
      m_env = env;
      m_api = api;
      m_gpuFn = sym("OrtSessionOptionsAppendExecutionProvider_CoreML");
      return true;
   }

   OrtModel* OrtRuntime::OpenModel(const std::string& onnxPath, bool preferGpu, bool& usedGpu, std::string& err)
   {
      usedGpu = false;
      if (!m_api) { err = "runtime not loaded"; return nullptr; }
      const OrtApi* api = A(m_api);
      OrtSessionOptions* so = nullptr;
      if (!Check(api, api->CreateSessionOptions(&so), err)) return nullptr;
      if (OrtStatus* st = api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL)) api->ReleaseStatus(st);
      if (preferGpu && m_gpuFn)
      {
         using Fn = OrtStatus* (*)(OrtSessionOptions*, uint32_t);
         OrtStatus* st = reinterpret_cast<Fn>(m_gpuFn)(so, 0);
         if (st) api->ReleaseStatus(st); else usedGpu = true;
      }
      OrtModel* m = new OrtModel();
#if defined(_WIN32)
      const std::wstring modelPath = AppPaths::FsPath(onnxPath).wstring(); // ORTCHAR_T is wchar_t on Windows
#else
      const std::string& modelPath = onnxPath;
#endif
      if (!Check(api, api->CreateSession(static_cast<OrtEnv*>(m_env), modelPath.c_str(), so, &m->session), err))
      {
         api->ReleaseSessionOptions(so);
         delete m;
         return nullptr;
      }
      api->ReleaseSessionOptions(so);

      OrtAllocator* alloc = nullptr;
      if (OrtStatus* st = api->GetAllocatorWithDefaultOptions(&alloc)) api->ReleaseStatus(st);
      size_t nIn = 0;
      if (OrtStatus* st = api->SessionGetInputCount(m->session, &nIn)) api->ReleaseStatus(st);
      for (size_t i = 0; i < nIn; ++i)
      {
         char* nm = nullptr;
         if (api->SessionGetInputName(m->session, i, alloc, &nm) == nullptr && nm)
         {
            m->inNames.push_back(nm);
            alloc->Free(alloc, nm);
         }
      }
      if (!m->inNames.empty()) m->inName = m->inNames[0];
      size_t nOut = 0;
      if (OrtStatus* st = api->SessionGetOutputCount(m->session, &nOut)) api->ReleaseStatus(st);
      for (size_t i = 0; i < nOut; ++i)
      {
         char* o = nullptr;
         if (api->SessionGetOutputName(m->session, i, alloc, &o) == nullptr && o)
         {
            m->outNames.push_back(o);
            alloc->Free(alloc, o);
         }
      }
      return m;
   }

   void OrtRuntime::CloseModel(OrtModel* m)
   {
      if (!m) return;
      if (m->session && m_api) A(m_api)->ReleaseSession(m->session);
      delete m;
   }

   bool OrtRuntime::Run(OrtModel* m, const float* input, const std::vector<int64_t>& inShape,
                        std::vector<Tensor>& outs, std::vector<std::string>& outNames, std::string& err)
   {
      if (!m || !m_api) { err = "no model"; return false; }
      const OrtApi* api = A(m_api);
      OrtMemoryInfo* mi = nullptr;
      if (!Check(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi), err)) return false;
      size_t count = 1;
      for (int64_t d : inShape) count *= static_cast<size_t>(d);
      OrtValue* in = nullptr;
      OrtStatus* st = api->CreateTensorWithDataAsOrtValue(mi, const_cast<float*>(input), count * sizeof(float),
                                                          inShape.data(), inShape.size(),
                                                          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in);
      api->ReleaseMemoryInfo(mi);
      if (!Check(api, st, err)) return false;

      std::vector<const char*> outN;
      for (auto& s : m->outNames) outN.push_back(s.c_str());
      std::vector<OrtValue*> res(outN.size(), nullptr);
      const char* inN = m->inName.c_str();
      st = api->Run(m->session, nullptr, &inN, &in, 1, outN.data(), outN.size(), res.data());
      api->ReleaseValue(in);
      if (!Check(api, st, err)) return false;

      outs.assign(res.size(), Tensor{});
      outNames = m->outNames;
      for (size_t i = 0; i < res.size(); ++i)
      {
         OrtTensorTypeAndShapeInfo* info = nullptr;
         if (api->GetTensorTypeAndShape(res[i], &info) == nullptr)
         {
            size_t nd = 0;
            if (OrtStatus* st = api->GetDimensionsCount(info, &nd)) api->ReleaseStatus(st);
            outs[i].shape.resize(nd);
            if (OrtStatus* st = api->GetDimensions(info, outs[i].shape.data(), nd)) api->ReleaseStatus(st);
            size_t n = 0;
            if (OrtStatus* st = api->GetTensorShapeElementCount(info, &n)) api->ReleaseStatus(st);
            float* p = nullptr;
            if (OrtStatus* st = api->GetTensorMutableData(res[i], reinterpret_cast<void**>(&p))) api->ReleaseStatus(st);
            if (p) outs[i].data.assign(p, p + n);
            api->ReleaseTensorTypeAndShapeInfo(info);
         }
         api->ReleaseValue(res[i]);
      }
      return true;
   }

   std::vector<std::string> OrtRuntime::InputNames(OrtModel* m) const
   {
      return m ? m->inNames : std::vector<std::string>{};
   }

   // IEEE half to float (no denormal shortcut: the masks use the full range).
   static float HalfToFloat(uint16_t h)
   {
      const uint32_t sign = (h & 0x8000u) << 16;
      uint32_t exp = (h >> 10) & 0x1Fu;
      uint32_t man = h & 0x3FFu;
      uint32_t bits;
      if (exp == 0)
      {
         if (man == 0) bits = sign;
         else
         {
            exp = 1;
            while (!(man & 0x400u)) { man <<= 1; --exp; }
            man &= 0x3FFu;
            bits = sign | ((exp + 112u) << 23) | (man << 13);
         }
      }
      else if (exp == 31) bits = sign | 0x7F800000u | (man << 13);
      else bits = sign | ((exp + 112u) << 23) | (man << 13);
      float f;
      std::memcpy(&f, &bits, sizeof f);
      return f;
   }

   bool OrtRuntime::RunNamed(OrtModel* m, const std::vector<Input>& ins, std::vector<Tensor>& outs, std::string& err)
   {
      if (!m || !m_api) { err = "no model"; return false; }
      const OrtApi* api = A(m_api);
      OrtMemoryInfo* mi = nullptr;
      if (!Check(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi), err)) return false;
      std::vector<OrtValue*> inVals;
      std::vector<const char*> inN;
      bool ok = true;
      for (const Input& in : ins)
      {
         size_t count = 1;
         for (int64_t d : in.shape) count *= static_cast<size_t>(d);
         OrtValue* v = nullptr;
         OrtStatus* st = in.i64
            ? api->CreateTensorWithDataAsOrtValue(mi, const_cast<int64_t*>(in.i64), count * sizeof(int64_t),
                                                  in.shape.data(), in.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &v)
            : api->CreateTensorWithDataAsOrtValue(mi, const_cast<float*>(in.f32), count * sizeof(float),
                                                  in.shape.data(), in.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &v);
         if (!Check(api, st, err)) { ok = false; break; }
         inVals.push_back(v);
         inN.push_back(in.name.c_str());
      }
      api->ReleaseMemoryInfo(mi);
      if (!ok)
      {
         for (OrtValue* v : inVals) api->ReleaseValue(v);
         return false;
      }
      std::vector<const char*> outN;
      for (auto& s : m->outNames) outN.push_back(s.c_str());
      std::vector<OrtValue*> res(outN.size(), nullptr);
      OrtStatus* st = api->Run(m->session, nullptr, inN.data(), inVals.data(), inVals.size(), outN.data(), outN.size(), res.data());
      for (OrtValue* v : inVals) api->ReleaseValue(v);
      if (!Check(api, st, err)) return false;

      outs.assign(res.size(), Tensor{});
      for (size_t i = 0; i < res.size(); ++i)
      {
         OrtTensorTypeAndShapeInfo* info = nullptr;
         if (api->GetTensorTypeAndShape(res[i], &info) == nullptr)
         {
            size_t nd = 0;
            if (OrtStatus* s2 = api->GetDimensionsCount(info, &nd)) api->ReleaseStatus(s2);
            outs[i].shape.resize(nd);
            if (OrtStatus* s2 = api->GetDimensions(info, outs[i].shape.data(), nd)) api->ReleaseStatus(s2);
            size_t n = 0;
            if (OrtStatus* s2 = api->GetTensorShapeElementCount(info, &n)) api->ReleaseStatus(s2);
            ONNXTensorElementDataType ty = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
            if (OrtStatus* s2 = api->GetTensorElementType(info, &ty)) api->ReleaseStatus(s2);
            void* p = nullptr;
            if (OrtStatus* s2 = api->GetTensorMutableData(res[i], &p)) api->ReleaseStatus(s2);
            if (p && ty == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
               outs[i].data.assign(static_cast<float*>(p), static_cast<float*>(p) + n);
            else if (p && ty == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16)
            {
               outs[i].data.resize(n);
               const uint16_t* h = static_cast<const uint16_t*>(p);
               for (size_t k = 0; k < n; ++k) outs[i].data[k] = HalfToFloat(h[k]);
            }
            api->ReleaseTensorTypeAndShapeInfo(info);
         }
         api->ReleaseValue(res[i]);
      }
      return true;
   }
}
