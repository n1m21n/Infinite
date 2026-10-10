#include "OrtRuntime.h"

#include "onnxruntime_c_api.h"
#include "platform/AppPaths.h"

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
      api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
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
      api->GetAllocatorWithDefaultOptions(&alloc);
      char* nm = nullptr;
      if (Check(api, api->SessionGetInputName(m->session, 0, alloc, &nm), err) && nm)
      {
         m->inName = nm;
         alloc->Free(alloc, nm);
      }
      size_t nOut = 0;
      api->SessionGetOutputCount(m->session, &nOut);
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
            api->GetDimensionsCount(info, &nd);
            outs[i].shape.resize(nd);
            api->GetDimensions(info, outs[i].shape.data(), nd);
            size_t n = 0;
            api->GetTensorShapeElementCount(info, &n);
            float* p = nullptr;
            api->GetTensorMutableData(res[i], reinterpret_cast<void**>(&p));
            if (p) outs[i].data.assign(p, p + n);
            api->ReleaseTensorTypeAndShapeInfo(info);
         }
         api->ReleaseValue(res[i]);
      }
      return true;
   }
}
