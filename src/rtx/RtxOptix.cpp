// Turbo 0.62: Render 3D's RTX mode on NVIDIA OptiX 7.7 (headers from
// github.com/NVIDIA/optix-dev, fetched at configure time; the ray tracer itself
// ships in the NVIDIA driver). Built only with INFINITE_ENABLE_RTX in the full
// edition; nvcuda.dll is delay-loaded, so the exe still starts on machines
// without an NVIDIA driver and the mode just reports why it is unavailable.
//
// Per frame, on the main (GL) thread:
//  1. every GL buffer the scene needs (PointZ attributes, triangle indices,
//     instance matrix columns) is copied GPU to GPU into one "arena" GL
//     buffer that is registered with CUDA once, then mapped - no downloads;
//  2. one acceleration structure per geometry, rebuilt only when its key
//     changes (CPU meshes are uploaded once per revision);
//  3. the instance list (from the matrix columns by a CUDA kernel) and the
//     top-level structure over it, rebuilt every frame (milliseconds);
//  4. one launch of RtxDevice.cu's ray generation program, accumulating while
//     nothing changes, then the OptiX AI denoiser (albedo + normal guides)
//     straight into a GL pixel buffer that feeds the output texture.

#include "nodes/RtxBackend.h"

#include <GL/glew.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cuda.h>
#include <cudaGL.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "RtxShared.h"
#include <vector_functions.h>

extern const unsigned char kRtxDeviceIr[];
extern const size_t kRtxDeviceIrSize;
extern const unsigned char kRtxKernelsPtx[];
extern const size_t kRtxKernelsPtxSize;

namespace
{
   template <typename T>
   struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord
   {
      char header[OPTIX_SBT_RECORD_HEADER_SIZE];
      T data;
   };
   struct Empty
   {
      int unused;
   };

   size_t Align(size_t v, size_t a) { return (v + a - 1) / a * a; }
   float ToLin(float c) { return std::pow(std::max(c, 0.0f), 2.2f); }
   float3 ToLin3(const float c[3]) { return make_float3(ToLin(c[0]), ToLin(c[1]), ToLin(c[2])); }
   constexpr size_t kNone = ~(size_t)0;

   // Device memory handed out front to back during one frame; Reset() starts
   // over (the previous frame's work has finished by then). When a frame
   // needs more than the block, extra blocks are made and merged next time.
   class FrameMem
   {
   public:
      // Frees everything; needs the CUDA context current (the owner's destructor).
      void Release() { FreeAll(); }
      void Reset()
      {
         if (mBlocks.size() > 1)
         {
            const size_t total = mTotal;
            FreeAll();
            Grow(total);
         }
         mUsed = 0;
         mCur = 0;
      }
      CUdeviceptr Get(size_t bytes)
      {
         bytes = Align(std::max<size_t>(bytes, 16), 256);
         if (mBlocks.empty() || mUsed + bytes > mBlocks[mCur].size)
         {
            if (!Grow(std::max<size_t>(bytes, mBlocks.empty() ? (size_t)(64u << 20) : mBlocks.back().size)))
               return 0;
            mCur = mBlocks.size() - 1;
            mUsed = 0;
         }
         const CUdeviceptr p = mBlocks[mCur].ptr + mUsed;
         mUsed += bytes;
         return p;
      }

   private:
      struct Block
      {
         CUdeviceptr ptr;
         size_t size;
      };
      bool Grow(size_t bytes)
      {
         Block b{ 0, bytes };
         if (cuMemAlloc(&b.ptr, bytes) != CUDA_SUCCESS)
            return false;
         mBlocks.push_back(b);
         mTotal += bytes;
         return true;
      }
      void FreeAll()
      {
         for (Block& b : mBlocks)
            cuMemFree(b.ptr);
         mBlocks.clear();
         mTotal = 0;
      }
      std::vector<Block> mBlocks;
      size_t mUsed = 0, mCur = 0, mTotal = 0;
   };

   class RtxOptix final : public RtxBackend
   {
   public:
      bool Init(std::string& why);
      ~RtxOptix() override;

      void SetScene(const std::vector<Geometry>& geometry, const std::vector<Light>& lights, const Env& env) override
      {
         mGeo = geometry;
         mLights = lights;
         mEnv = env;
      }
      unsigned int Render(const Frame& frame) override;
      int Samples() const override { return mSamples; }
      double LastMs() const override { return mLastMs; }
      const std::string& Error() const override { return mError; }

   private:
      struct Gas
      {
         CUdeviceptr buf = 0;
         OptixTraversableHandle handle = 0;
         int lastUse = 0;
      };
      // Turbo 0.62: one structure per animated source, refitted while its
      // counts stay the same (rebuilt every kRefitLimit refits for quality).
      struct SlotGas
      {
         CUdeviceptr buf = 0;
         size_t bytes = 0;
         OptixTraversableHandle handle = 0;
         int kind = -1, prims = 0, verts = 0, refits = 0, lastUse = 0;
      };
      static constexpr int kRefitLimit = 60;
      struct MeshMem
      {
         CUdeviceptr verts = 0, idx = 0, color = 0;
         int lastUse = 0;
      };
      struct TexReg
      {
         CUgraphicsResource res = nullptr;
         int w = 0, h = 0;
         GLint format = 0;
         int lastUse = 0;
         CUtexObject obj = 0;
      };
      // Where one geometry's GL data landed in the arena (byte offsets).
      struct Slots
      {
         size_t pos = kNone, normal = kNone, uv = kNone, color = kNone, idx = kNone, radius = kNone,
                inst[4] = { kNone, kNone, kNone, kNone }, instColor = kNone;
      };

      bool Fail(const std::string& what)
      {
         mError = what;
         return false;
      }
      bool Check(OptixResult r, const char* what)
      {
         if (r == OPTIX_SUCCESS)
            return true;
         return Fail(std::string("OptiX ") + what + ": " + optixGetErrorName(r));
      }
      bool Check(CUresult r, const char* what)
      {
         if (r == CUDA_SUCCESS)
            return true;
         const char* name = nullptr;
         cuGetErrorName(r, &name);
         return Fail(std::string("CUDA ") + what + ": " + (name ? name : "error"));
      }
      bool EnsureOutput(int w, int h);
      bool EnsureArena(size_t bytes);
      TexReg* RegisterTex(unsigned int tex);
      bool BuildGas(const Geometry& g, const Slots& s, CUdeviceptr arena, Gas& out, RtxGeomData& data);
      void Evict();
      bool Trace(const Frame& f);

      CUcontext mCtx = nullptr;
      CUdevice mDev = 0;
      CUstream mStream = nullptr;
      OptixDeviceContext mOx = nullptr;
      OptixModule mModule = nullptr, mSphereModule = nullptr;
      OptixProgramGroup mRaygen = nullptr, mMiss = nullptr, mHitTri = nullptr, mHitSphere = nullptr;
      OptixPipeline mPipeline = nullptr;
      CUmodule mKernels = nullptr;
      CUfunction mBuildInstances = nullptr, mRadii = nullptr, mDemod = nullptr, mAtrous = nullptr, mRemod = nullptr;
      CUdeviceptr mRaygenRec = 0, mMissRec = 0;
      OptixDenoiser mDenoiser = nullptr;
      CUdeviceptr mDenState = 0, mDenScratch = 0, mIntensity = 0;
      size_t mDenStateBytes = 0, mDenScratchBytes = 0;

      int mW = 0, mH = 0;
      CUdeviceptr mAccum = 0, mAverage = 0, mAlbedo = 0, mNormal = 0, mTmp[2] = { 0, 0 };
      GLuint mPbo = 0, mTex = 0;
      CUgraphicsResource mPboRes = nullptr;
      bool mUpload = false; // the pixel buffer holds a new image for mTex

      GLuint mArena = 0;
      size_t mArenaBytes = 0;
      CUgraphicsResource mArenaRes = nullptr;

      CUdeviceptr mIas = 0;
      size_t mIasBytes = 0;
      FrameMem mFrame;
      std::unordered_map<uint64_t, Gas> mGas;
      std::unordered_map<uint64_t, SlotGas> mSlotGas;
      std::unordered_map<uint64_t, MeshMem> mMeshes;
      std::unordered_map<unsigned int, TexReg> mTexs;
      std::vector<CUtexObject> mTexObjects; // made this frame, destroyed next frame
      CUevent mEvStart = nullptr, mEvEnd = nullptr, mEvGeo = nullptr, mEvTrace = nullptr;
      // INFINITE_FRAMEPROF: GPU time per stage, printed every 120 traces.
      bool mProf = getenv("INFINITE_FRAMEPROF") != nullptr;
      double mProfSum[3] = {};
      int mProfN = 0;
      bool mTimed = false;

      std::vector<Geometry> mGeo;
      std::vector<Light> mLights;
      Env mEnv;
      int mSamples = 0;
      int mFrameNo = 0;
      unsigned int mSeed = 1;
      double mLastMs = 0.0;
      std::string mError;
   };

   void LogCallback(unsigned int, const char*, const char*, void*) {}

   bool RtxOptix::Init(std::string& why)
   {
      // The driver is delay-loaded: no nvcuda.dll means no NVIDIA driver at all.
      if (LoadLibraryA("nvcuda.dll") == nullptr)
      {
         why = "RTX needs an NVIDIA GPU (no NVIDIA driver found)";
         return false;
      }
      if (cuInit(0) != CUDA_SUCCESS)
      {
         why = "RTX: the NVIDIA driver did not start CUDA";
         return false;
      }
      // The CUDA device that runs this GL context.
      unsigned int count = 0;
      if (cuGLGetDevices(&count, &mDev, 1, CU_GL_DEVICE_LIST_ALL) != CUDA_SUCCESS || count == 0)
      {
         why = "RTX: the window is not on an NVIDIA GPU";
         return false;
      }
      int major = 0, minor = 0;
      cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, mDev);
      cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, mDev);
      if (major * 10 + minor < 75)
      {
         why = "RTX needs an RTX GPU (GeForce RTX 20 series or newer)";
         return false;
      }
      if (cuDevicePrimaryCtxRetain(&mCtx, mDev) != CUDA_SUCCESS)
      {
         mCtx = nullptr;
         why = "RTX: could not create a CUDA context";
         return false;
      }
      cuCtxPushCurrent(mCtx);
      struct Pop
      {
         ~Pop()
         {
            CUcontext c;
            cuCtxPopCurrent(&c);
         }
      } pop;
      cuStreamCreate(&mStream, CU_STREAM_NON_BLOCKING);
      cuEventCreate(&mEvStart, CU_EVENT_DEFAULT);
      cuEventCreate(&mEvEnd, CU_EVENT_DEFAULT);
      cuEventCreate(&mEvGeo, CU_EVENT_DEFAULT);
      cuEventCreate(&mEvTrace, CU_EVENT_DEFAULT);

      if (optixInit() != OPTIX_SUCCESS)
      {
         why = "RTX: the NVIDIA driver is too old for OptiX 7.7 (update to 530 or newer)";
         return false;
      }
      OptixDeviceContextOptions co = {};
      co.logCallbackFunction = &LogCallback;
      co.logCallbackLevel = 1;
      if (optixDeviceContextCreate(mCtx, &co, &mOx) != OPTIX_SUCCESS)
      {
         why = "RTX: could not create the OptiX context";
         return false;
      }

      OptixModuleCompileOptions mco = {};
      mco.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
      mco.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
      OptixPipelineCompileOptions pco = {};
      pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
      pco.numPayloadValues = 2;
      pco.numAttributeValues = 2;
      pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
      pco.pipelineLaunchParamsVariableName = "params";
      pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_SPHERE;
      char log[2048];
      size_t logSize = sizeof(log);
      if (optixModuleCreate(mOx, &mco, &pco, (const char*)kRtxDeviceIr, kRtxDeviceIrSize, log, &logSize, &mModule) !=
          OPTIX_SUCCESS)
      {
         why = std::string("RTX: device code did not load: ") + log;
         return false;
      }
      OptixBuiltinISOptions bis = {};
      bis.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_SPHERE;
      if (optixBuiltinISModuleGet(mOx, &mco, &pco, &bis, &mSphereModule) != OPTIX_SUCCESS)
      {
         why = "RTX: no built-in sphere support in this driver";
         return false;
      }
      OptixProgramGroupOptions pgo = {};
      OptixProgramGroupDesc d[4] = {};
      d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
      d[0].raygen.module = mModule;
      d[0].raygen.entryFunctionName = "__raygen__render";
      d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
      d[1].miss.module = mModule;
      d[1].miss.entryFunctionName = "__miss__radiance";
      d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
      d[2].hitgroup.moduleCH = mModule;
      d[2].hitgroup.entryFunctionNameCH = "__closesthit__tri";
      d[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
      d[3].hitgroup.moduleCH = mModule;
      d[3].hitgroup.entryFunctionNameCH = "__closesthit__sphere";
      d[3].hitgroup.moduleIS = mSphereModule;
      d[3].hitgroup.entryFunctionNameIS = nullptr;
      OptixProgramGroup groups[4] = {};
      logSize = sizeof(log);
      if (optixProgramGroupCreate(mOx, d, 4, &pgo, log, &logSize, groups) != OPTIX_SUCCESS)
      {
         why = std::string("RTX: program groups: ") + log;
         return false;
      }
      mRaygen = groups[0];
      mMiss = groups[1];
      mHitTri = groups[2];
      mHitSphere = groups[3];
      OptixPipelineLinkOptions plo = {};
      plo.maxTraceDepth = 1;
      logSize = sizeof(log);
      if (optixPipelineCreate(mOx, &pco, &plo, groups, 4, log, &logSize, &mPipeline) != OPTIX_SUCCESS)
      {
         why = std::string("RTX: pipeline: ") + log;
         return false;
      }

      if (cuModuleLoadData(&mKernels, kRtxKernelsPtx) != CUDA_SUCCESS ||
          cuModuleGetFunction(&mBuildInstances, mKernels, "rtxBuildInstances") != CUDA_SUCCESS ||
          cuModuleGetFunction(&mRadii, mKernels, "rtxRadii") != CUDA_SUCCESS ||
          cuModuleGetFunction(&mDemod, mKernels, "rtxDemod") != CUDA_SUCCESS ||
          cuModuleGetFunction(&mAtrous, mKernels, "rtxAtrous") != CUDA_SUCCESS ||
          cuModuleGetFunction(&mRemod, mKernels, "rtxRemod") != CUDA_SUCCESS)
      {
         why = "RTX: helper kernels did not load";
         return false;
      }

      SbtRecord<Empty> rg = {}, ms = {};
      optixSbtRecordPackHeader(mRaygen, &rg);
      optixSbtRecordPackHeader(mMiss, &ms);
      cuMemAlloc(&mRaygenRec, sizeof(rg));
      cuMemAlloc(&mMissRec, sizeof(ms));
      cuMemcpyHtoD(mRaygenRec, &rg, sizeof(rg));
      cuMemcpyHtoD(mMissRec, &ms, sizeof(ms));

      OptixDenoiserOptions dop = {};
      dop.guideAlbedo = 1;
      dop.guideNormal = 1;
      if (optixDenoiserCreate(mOx, OPTIX_DENOISER_MODEL_KIND_HDR, &dop, &mDenoiser) != OPTIX_SUCCESS)
         mDenoiser = nullptr; // traces without denoising
      cuMemAlloc(&mIntensity, sizeof(float));
      return true;
   }

   RtxOptix::~RtxOptix()
   {
      if (mCtx == nullptr)
         return;
      cuCtxPushCurrent(mCtx);
      if (mStream)
         cuStreamSynchronize(mStream);
      for (CUtexObject t : mTexObjects)
         cuTexObjectDestroy(t);
      for (auto& kv : mTexs)
         if (kv.second.res)
            cuGraphicsUnregisterResource(kv.second.res);
      for (auto& kv : mGas)
         cuMemFree(kv.second.buf);
      for (auto& kv : mSlotGas)
         cuMemFree(kv.second.buf);
      for (auto& kv : mMeshes)
      {
         cuMemFree(kv.second.verts);
         cuMemFree(kv.second.idx);
         if (kv.second.color)
            cuMemFree(kv.second.color);
      }
      if (mArenaRes)
         cuGraphicsUnregisterResource(mArenaRes);
      if (mPboRes)
         cuGraphicsUnregisterResource(mPboRes);
      const CUdeviceptr bufs[] = { mAccum, mAverage, mAlbedo, mNormal, mTmp[0], mTmp[1], mIas, mRaygenRec, mMissRec, mDenState, mDenScratch,
                                   mIntensity };
      for (CUdeviceptr b : bufs)
         if (b)
            cuMemFree(b);
      mFrame.Release();
      if (mDenoiser)
         optixDenoiserDestroy(mDenoiser);
      if (mPipeline)
         optixPipelineDestroy(mPipeline);
      const OptixProgramGroup groups[] = { mRaygen, mMiss, mHitTri, mHitSphere };
      for (OptixProgramGroup g : groups)
         if (g)
            optixProgramGroupDestroy(g);
      if (mModule)
         optixModuleDestroy(mModule);
      if (mOx)
         optixDeviceContextDestroy(mOx);
      if (mKernels)
         cuModuleUnload(mKernels);
      if (mEvStart)
         cuEventDestroy(mEvStart);
      if (mEvEnd)
         cuEventDestroy(mEvEnd);
      if (mEvGeo)
         cuEventDestroy(mEvGeo);
      if (mEvTrace)
         cuEventDestroy(mEvTrace);
      if (mStream)
         cuStreamDestroy(mStream);
      CUcontext c;
      cuCtxPopCurrent(&c);
      cuDevicePrimaryCtxRelease(mDev);
      if (mArena)
         glDeleteBuffers(1, &mArena);
      if (mPbo)
         glDeleteBuffers(1, &mPbo);
      if (mTex)
         glDeleteTextures(1, &mTex);
   }

   bool RtxOptix::EnsureOutput(int w, int h)
   {
      if (w == mW && h == mH && mTex != 0)
         return true;
      CUdeviceptr* bufs[] = { &mAccum, &mAverage, &mAlbedo, &mNormal, &mTmp[0], &mTmp[1] };
      for (CUdeviceptr* b : bufs)
         if (*b)
         {
            cuMemFree(*b);
            *b = 0;
         }
      if (mPboRes)
      {
         cuGraphicsUnregisterResource(mPboRes);
         mPboRes = nullptr;
      }
      mW = mH = 0;
      const size_t bytes = (size_t)w * h * 16;
      for (CUdeviceptr* b : bufs)
         if (!Check(cuMemAlloc(b, bytes), "output buffers"))
            return false;
      if (mPbo == 0)
         glGenBuffers(1, &mPbo);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPbo);
      glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_DRAW);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      if (!Check(cuGraphicsGLRegisterBuffer(&mPboRes, mPbo, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD), "output buffer"))
         return false;
      if (mTex != 0)
         glDeleteTextures(1, &mTex);
      glGenTextures(1, &mTex);
      glBindTexture(GL_TEXTURE_2D, mTex);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      if (mDenoiser)
      {
         OptixDenoiserSizes sz = {};
         if (optixDenoiserComputeMemoryResources(mDenoiser, w, h, &sz) == OPTIX_SUCCESS)
         {
            if (mDenState)
               cuMemFree(mDenState);
            if (mDenScratch)
               cuMemFree(mDenScratch);
            mDenStateBytes = sz.stateSizeInBytes;
            mDenScratchBytes = std::max(sz.withoutOverlapScratchSizeInBytes, sz.computeIntensitySizeInBytes);
            cuMemAlloc(&mDenState, mDenStateBytes);
            cuMemAlloc(&mDenScratch, mDenScratchBytes);
            if (optixDenoiserSetup(mDenoiser, mStream, w, h, mDenState, mDenStateBytes, mDenScratch, mDenScratchBytes) !=
                OPTIX_SUCCESS)
            {
               optixDenoiserDestroy(mDenoiser);
               mDenoiser = nullptr;
            }
         }
      }
      mW = w;
      mH = h;
      mSamples = 0;
      return true;
   }

   bool RtxOptix::EnsureArena(size_t bytes)
   {
      if (bytes <= mArenaBytes && mArena != 0)
         return true;
      if (mArenaRes)
      {
         cuGraphicsUnregisterResource(mArenaRes);
         mArenaRes = nullptr;
      }
      const size_t want = std::max(Align(bytes + bytes / 4, 1u << 20), (size_t)(16u << 20));
      if (mArena == 0)
         glGenBuffers(1, &mArena);
      glBindBuffer(GL_COPY_WRITE_BUFFER, mArena);
      glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)want, nullptr, GL_DYNAMIC_COPY);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
      mArenaBytes = 0;
      if (!Check(cuGraphicsGLRegisterBuffer(&mArenaRes, mArena, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "arena"))
         return false;
      mArenaBytes = want;
      return true;
   }

   RtxOptix::TexReg* RtxOptix::RegisterTex(unsigned int tex)
   {
      if (tex == 0 || !glIsTexture(tex))
         return nullptr;
      GLint w = 0, h = 0, fmt = 0;
      glBindTexture(GL_TEXTURE_2D, tex);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
      glBindTexture(GL_TEXTURE_2D, 0);
      auto it = mTexs.find(tex);
      if (it != mTexs.end() && (it->second.w != w || it->second.h != h || it->second.format != fmt))
      {
         cuGraphicsUnregisterResource(it->second.res);
         mTexs.erase(it);
         it = mTexs.end();
      }
      if (it == mTexs.end())
      {
         TexReg r;
         r.w = w;
         r.h = h;
         r.format = fmt;
         if (w <= 0 || h <= 0 ||
             cuGraphicsGLRegisterImage(&r.res, tex, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY) != CUDA_SUCCESS)
            return nullptr;
         it = mTexs.emplace(tex, r).first;
      }
      it->second.lastUse = mFrameNo;
      it->second.obj = 0;
      return &it->second;
   }

   bool RtxOptix::BuildGas(const Geometry& g, const Slots& s, CUdeviceptr arena, Gas& out, RtxGeomData& data)
   {
      std::memset(&data, 0, sizeof(data));
      auto at = [arena](size_t off) -> CUdeviceptr { return off == kNone ? 0 : arena + off; };
      OptixBuildInput in = {};
      unsigned int flags = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
      CUdeviceptr vert = 0, radius = 0;
      unsigned int buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;

      if (g.kind == Geometry::kSpheres)
      {
         data.kind = 1;
         if (!g.cpuCentres.empty())
         {
            const size_t bytes = g.cpuCentres.size() * sizeof(float);
            vert = mFrame.Get(bytes);
            cuMemcpyHtoDAsync(vert, g.cpuCentres.data(), bytes, mStream);
            data.pos = (const float*)vert;
            data.posStride = 4;
            radius = vert + 12;
            if (!g.cpuColors.empty())
            {
               const CUdeviceptr c = mFrame.Get(g.cpuColors.size() * sizeof(float));
               cuMemcpyHtoDAsync(c, g.cpuColors.data(), g.cpuColors.size() * sizeof(float), mStream);
               data.color = (const float*)c;
               data.colorStride = 3;
            }
            in.sphereArray.vertexStrideInBytes = 16;
            in.sphereArray.radiusStrideInBytes = 16;
         }
         else
         {
            vert = at(s.pos);
            data.pos = (const float*)vert;
            data.posStride = g.posComps;
            radius = mFrame.Get((size_t)g.pointCount * sizeof(float));
            const float* ps = (const float*)at(s.radius);
            int stride = 1, count = g.pointCount;
            float scale = g.radiusScale;
            void* args[] = { &radius, &ps, &stride, &scale, &count };
            cuLaunchKernel(mRadii, (unsigned)((count + 255) / 256), 1, 1, 256, 1, 1, 0, mStream, args, nullptr);
            in.sphereArray.vertexStrideInBytes = (unsigned)(g.posComps * 4);
            in.sphereArray.radiusStrideInBytes = 4;
            if (s.color != kNone && g.colorComps >= 3)
            {
               data.color = (const float*)at(s.color);
               data.colorStride = g.colorComps;
            }
         }
         data.radius = (const float*)radius;
         in.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
         in.sphereArray.vertexBuffers = &vert;
         in.sphereArray.numVertices = (unsigned)g.pointCount;
         in.sphereArray.radiusBuffers = &radius;
         in.sphereArray.flags = &flags;
         in.sphereArray.numSbtRecords = 1;
      }
      else
      {
         data.kind = 0;
         CUdeviceptr idx = 0;
         int vstride = 3;
         if (g.mesh != nullptr)
         {
            MeshMem& m = mMeshes[g.key];
            if (m.verts == 0)
            {
               const size_t vb = g.mesh->vertices.size() * sizeof(Vertex), ib = g.mesh->indices.size() * 4;
               if (!Check(cuMemAlloc(&m.verts, vb), "mesh") || !Check(cuMemAlloc(&m.idx, ib), "mesh"))
                  return false;
               cuMemcpyHtoD(m.verts, g.mesh->vertices.data(), vb);
               cuMemcpyHtoD(m.idx, g.mesh->indices.data(), ib);
               if (g.mesh->HasVertexColor())
               {
                  cuMemAlloc(&m.color, g.mesh->vertexColor.size() * 4);
                  cuMemcpyHtoD(m.color, g.mesh->vertexColor.data(), g.mesh->vertexColor.size() * 4);
               }
            }
            m.lastUse = mFrameNo;
            vert = m.verts;
            idx = m.idx;
            vstride = (int)(sizeof(Vertex) / 4);
            data.pos = (const float*)m.verts;
            data.posStride = vstride;
            data.normal = (const float*)m.verts + 3;
            data.normalStride = vstride;
            data.uv = (const float*)m.verts + 6;
            data.uvStride = vstride;
            if (m.color)
            {
               data.color = (const float*)m.color;
               data.colorStride = 3;
            }
            buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
         }
         else
         {
            vert = at(s.pos);
            idx = at(s.idx);
            vstride = g.posComps;
            data.pos = (const float*)vert;
            data.posStride = vstride;
            if (s.normal != kNone && g.normalComps >= 3)
            {
               data.normal = (const float*)at(s.normal);
               data.normalStride = g.normalComps;
            }
            if (s.uv != kNone && g.uvComps >= 2)
            {
               data.uv = (const float*)at(s.uv);
               data.uvStride = g.uvComps;
            }
            if (s.color != kNone && g.colorComps >= 3)
            {
               data.color = (const float*)at(s.color);
               data.colorStride = g.colorComps;
            }
         }
         if (vert == 0 || idx == 0)
            return Fail("geometry without positions or triangles");
         data.idx = (const unsigned int*)idx;
         in.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
         in.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
         in.triangleArray.vertexStrideInBytes = (unsigned)(vstride * 4);
         in.triangleArray.numVertices = (unsigned)g.pointCount;
         in.triangleArray.vertexBuffers = &vert;
         in.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
         in.triangleArray.indexStrideInBytes = 12;
         in.triangleArray.numIndexTriplets = (unsigned)g.triCount;
         in.triangleArray.indexBuffer = idx;
         in.triangleArray.flags = &flags;
         in.triangleArray.numSbtRecords = 1;
      }

      // Animated GPU data (PointZ, sprites): refit this source's structure
      // when only the positions moved.
      if (g.mesh == nullptr && g.cpuCentres.empty() && g.slot != 0)
      {
         SlotGas& sg = mSlotGas[g.slot ^ ((uint64_t)g.kind << 62)];
         const int prims = g.kind == Geometry::kSpheres ? g.pointCount : g.triCount;
         const bool refit = sg.buf != 0 && sg.kind == (int)g.kind && sg.prims == prims && sg.verts == g.pointCount &&
                            sg.refits < kRefitLimit;
         OptixAccelBuildOptions opt = {};
         opt.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
         opt.operation = refit ? OPTIX_BUILD_OPERATION_UPDATE : OPTIX_BUILD_OPERATION_BUILD;
         OptixAccelBufferSizes sz = {};
         if (!Check(optixAccelComputeMemoryUsage(mOx, &opt, &in, 1, &sz), "geometry size"))
            return false;
         const size_t tempBytes = refit ? sz.tempUpdateSizeInBytes : sz.tempSizeInBytes;
         const CUdeviceptr temp = mFrame.Get(tempBytes);
         if (temp == 0)
            return Fail("out of GPU memory for the geometry");
         if (!refit && sg.bytes < sz.outputSizeInBytes)
         {
            if (sg.buf)
               cuMemFree(sg.buf);
            sg.bytes = Align(sz.outputSizeInBytes + sz.outputSizeInBytes / 8, 128);
            if (!Check(cuMemAlloc(&sg.buf, sg.bytes), "geometry memory"))
            {
               sg = SlotGas();
               return false;
            }
         }
         if (!Check(optixAccelBuild(mOx, mStream, &opt, &in, 1, temp, tempBytes, sg.buf, sg.bytes, &sg.handle, nullptr, 0),
                    refit ? "geometry refit" : "geometry build"))
         {
            cuMemFree(sg.buf);
            sg = SlotGas();
            return false;
         }
         sg.kind = (int)g.kind;
         sg.prims = prims;
         sg.verts = g.pointCount;
         sg.refits = refit ? sg.refits + 1 : 0;
         sg.lastUse = mFrameNo;
         out.buf = sg.buf;
         out.handle = sg.handle;
         out.lastUse = mFrameNo;
      }
      else
      {
      // The structure for this key may exist already (static geometry).
      uint64_t key = g.key ^ ((uint64_t)g.kind << 63);
      if (g.kind == Geometry::kSpheres)
      {
         uint32_t bits;
         std::memcpy(&bits, &g.radiusScale, 4);
         key ^= (uint64_t)bits * 0x100000001B3ull;
      }
      auto found = mGas.find(key);
      if (found != mGas.end())
      {
         found->second.lastUse = mFrameNo;
         out = found->second;
      }
      else
      {
         OptixAccelBuildOptions opt = {};
         opt.buildFlags = buildFlags;
         opt.operation = OPTIX_BUILD_OPERATION_BUILD;
         OptixAccelBufferSizes sz = {};
         if (!Check(optixAccelComputeMemoryUsage(mOx, &opt, &in, 1, &sz), "geometry size"))
            return false;
         const CUdeviceptr temp = mFrame.Get(sz.tempSizeInBytes);
         Gas gas;
         if (temp == 0 || !Check(cuMemAlloc(&gas.buf, Align(sz.outputSizeInBytes, 128)), "geometry memory"))
            return Fail("out of GPU memory for the geometry");
         if (!Check(optixAccelBuild(mOx, mStream, &opt, &in, 1, temp, sz.tempSizeInBytes, gas.buf, sz.outputSizeInBytes,
                                    &gas.handle, nullptr, 0),
                    "geometry build"))
         {
            cuMemFree(gas.buf);
            return false;
         }
         gas.lastUse = mFrameNo;
         mGas[key] = gas;
         out = gas;
      }
      } // static / CPU geometry

      const Material& m = g.material;
      data.base = ToLin3(m.color);
      data.metal = m.metallic;
      data.rough = m.roughness;
      data.opacity = m.opacity;
      data.ior = std::max(m.ior, 1.0f);
      data.trans = m.transmission;
      const float em = std::max(0.0f, m.emission);
      data.emission =
         make_float3(ToLin(m.emissionColor[0]) * em, ToLin(m.emissionColor[1]) * em, ToLin(m.emissionColor[2]) * em);
      return true;
   }

   void RtxOptix::Evict()
   {
      for (auto it = mGas.begin(); it != mGas.end();)
         if (it->second.lastUse != mFrameNo)
         {
            cuMemFree(it->second.buf);
            it = mGas.erase(it);
         }
         else
            ++it;
      for (auto it = mSlotGas.begin(); it != mSlotGas.end();)
         if (it->second.lastUse != mFrameNo)
         {
            cuMemFree(it->second.buf);
            it = mSlotGas.erase(it);
         }
         else
            ++it;
      for (auto it = mMeshes.begin(); it != mMeshes.end();)
         if (it->second.lastUse != mFrameNo)
         {
            cuMemFree(it->second.verts);
            cuMemFree(it->second.idx);
            if (it->second.color)
               cuMemFree(it->second.color);
            it = mMeshes.erase(it);
         }
         else
            ++it;
      for (auto it = mTexs.begin(); it != mTexs.end();)
         if (it->second.lastUse != mFrameNo)
         {
            cuGraphicsUnregisterResource(it->second.res);
            it = mTexs.erase(it);
         }
         else
            ++it;
   }

   unsigned int RtxOptix::Render(const Frame& f)
   {
      if (f.width <= 0 || f.height <= 0)
         return 0;
      cuCtxPushCurrent(mCtx);
      const bool ok = Trace(f);
      CUcontext c;
      cuCtxPopCurrent(&c);
      if (!ok)
         return 0;
      if (mUpload)
      {
         // The unmap at the end of Trace ordered CUDA's writes before this.
         glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPbo);
         glBindTexture(GL_TEXTURE_2D, mTex);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mW, mH, GL_RGBA, GL_FLOAT, nullptr);
         glBindTexture(GL_TEXTURE_2D, 0);
         glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
         mUpload = false;
      }
      return mTex;
   }

   bool RtxOptix::Trace(const Frame& f)
   {
      mError.clear();
      // The previous frame's work: done by now in practice; this also makes
      // its scratch memory and texture objects safe to reuse.
      cuStreamSynchronize(mStream);
      if (mTimed && cuEventQuery(mEvEnd) == CUDA_SUCCESS)
      {
         float ms = 0.0f;
         if (cuEventElapsedTime(&ms, mEvStart, mEvEnd) == CUDA_SUCCESS)
            mLastMs = ms;
         if (mProf)
         {
            float a = 0.0f, b = 0.0f, c = 0.0f;
            cuEventElapsedTime(&a, mEvStart, mEvGeo);
            cuEventElapsedTime(&b, mEvGeo, mEvTrace);
            cuEventElapsedTime(&c, mEvTrace, mEvEnd);
            mProfSum[0] += a;
            mProfSum[1] += b;
            mProfSum[2] += c;
            if (++mProfN == 120)
            {
               printf("RTXPROF structures=%.2f trace=%.2f denoise=%.2f ms\n", mProfSum[0] / mProfN, mProfSum[1] / mProfN,
                      mProfSum[2] / mProfN);
               mProfSum[0] = mProfSum[1] = mProfSum[2] = 0.0;
               mProfN = 0;
            }
         }
      }
      for (CUtexObject t : mTexObjects)
         cuTexObjectDestroy(t);
      mTexObjects.clear();
      mFrame.Reset();

      if (!EnsureOutput(f.width, f.height))
         return false;
      if (f.restart)
         mSamples = 0;
      if (mSamples >= f.maxSamples)
         return true;
      mFrameNo++;

      // 1. GL data into the arena.
      std::vector<Slots> slots(mGeo.size());
      size_t arenaUsed = 0;
      struct Copy
      {
         GLuint src;
         size_t off, bytes;
      };
      std::vector<Copy> copies;
      auto place = [&](GLuint buf, size_t bytes, size_t& slot) {
         if (buf == 0 || bytes == 0)
            return;
         slot = arenaUsed;
         copies.push_back({ buf, arenaUsed, bytes });
         arenaUsed = Align(arenaUsed + bytes, 256);
      };
      for (size_t i = 0; i < mGeo.size(); i++)
      {
         const Geometry& g = mGeo[i];
         Slots& s = slots[i];
         const size_t n = (size_t)g.pointCount;
         if (g.mesh == nullptr && g.cpuCentres.empty())
         {
            place(g.posBuf, n * g.posComps * 4, s.pos);
            place(g.normalBuf, n * g.normalComps * 4, s.normal);
            place(g.uvBuf, n * g.uvComps * 4, s.uv);
            place(g.colorBuf, n * g.colorComps * 4, s.color);
            place(g.indexBuf, (size_t)g.triCount * 12, s.idx);
            place(g.radiusBuf, n * 4, s.radius);
         }
         if (g.instCount > 0)
         {
            for (int c = 0; c < 4; c++)
               place(g.instCols[c], (size_t)g.instCount * 16, s.inst[c]);
            place(g.instColorBuf, (size_t)g.instCount * g.instColorComps * 4, s.instColor);
         }
      }
      if (!EnsureArena(std::max<size_t>(arenaUsed, 256)))
         return false;
      glBindBuffer(GL_COPY_WRITE_BUFFER, mArena);
      for (const Copy& c : copies)
      {
         glBindBuffer(GL_COPY_READ_BUFFER, c.src);
         glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, (GLintptr)c.off, (GLsizeiptr)c.bytes);
      }
      glBindBuffer(GL_COPY_READ_BUFFER, 0);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

      // 2. Map everything CUDA reads or writes this frame in one call.
      std::vector<unsigned int> geoTex(mGeo.size(), 0);
      for (size_t i = 0; i < mGeo.size(); i++)
         if (RegisterTex(mGeo[i].surfaceTex) != nullptr)
            geoTex[i] = mGeo[i].surfaceTex;
      const unsigned int envTex = RegisterTex(mEnv.tex) != nullptr ? mEnv.tex : 0;
      std::vector<CUgraphicsResource> res = { mArenaRes, mPboRes };
      for (auto& kv : mTexs)
         if (kv.second.lastUse == mFrameNo)
            res.push_back(kv.second.res);
      if (!Check(cuGraphicsMapResources((unsigned)res.size(), res.data(), mStream), "map"))
         return false;
      struct Unmap
      {
         std::vector<CUgraphicsResource>& r;
         CUstream s;
         ~Unmap() { cuGraphicsUnmapResources((unsigned)r.size(), r.data(), s); }
      } unmap{ res, mStream };
      CUdeviceptr arena = 0, out = 0;
      size_t mappedBytes = 0;
      cuGraphicsResourceGetMappedPointer(&arena, &mappedBytes, mArenaRes);
      cuGraphicsResourceGetMappedPointer(&out, &mappedBytes, mPboRes);
      for (auto& kv : mTexs)
      {
         TexReg& r = kv.second;
         if (r.lastUse != mFrameNo)
            continue;
         CUarray arr = nullptr;
         if (cuGraphicsSubResourceGetMappedArray(&arr, r.res, 0, 0) != CUDA_SUCCESS)
            continue;
         CUDA_RESOURCE_DESC rd = {};
         rd.resType = CU_RESOURCE_TYPE_ARRAY;
         rd.res.array.hArray = arr;
         CUDA_TEXTURE_DESC td = {};
         td.addressMode[0] = CU_TR_ADDRESS_MODE_WRAP;
         td.addressMode[1] = kv.first == envTex ? CU_TR_ADDRESS_MODE_CLAMP : CU_TR_ADDRESS_MODE_WRAP;
         td.filterMode = CU_TR_FILTER_MODE_LINEAR;
         td.flags = CU_TRSF_NORMALIZED_COORDINATES;
         if (cuTexObjectCreate(&r.obj, &rd, &td, nullptr) == CUDA_SUCCESS)
            mTexObjects.push_back(r.obj);
         else
            r.obj = 0;
      }
      auto texObj = [this](unsigned int tex) -> CUtexObject {
         auto it = mTexs.find(tex);
         return tex != 0 && it != mTexs.end() ? it->second.obj : 0;
      };
      cuEventRecord(mEvStart, mStream);

      // 3. Geometry structures, hit records, instances.
      using HitRecord = SbtRecord<RtxGeomData>;
      std::vector<HitRecord> records;
      std::vector<OptixInstance> hostInst;
      struct GpuInst
      {
         size_t first;
         int count;
         size_t cols[4];
         OptixTraversableHandle gas;
         unsigned int sbt;
      };
      std::vector<GpuInst> gpuInst;
      size_t instTotal = 0;
      for (size_t i = 0; i < mGeo.size(); i++)
      {
         const Geometry& g = mGeo[i];
         Gas gas;
         HitRecord rec = {};
         if (!BuildGas(g, slots[i], arena, gas, rec.data))
            return false;
         rec.data.tex = texObj(geoTex[i]);
         if (slots[i].instColor != kNone && g.instColorComps >= 3)
         {
            rec.data.instColor = (const float*)(arena + slots[i].instColor);
            rec.data.instColorStride = g.instColorComps;
         }
         optixSbtRecordPackHeader(g.kind == Geometry::kSpheres ? mHitSphere : mHitTri, &rec);
         const unsigned int sbt = (unsigned int)records.size();
         records.push_back(rec);
         if (g.instCount > 0 && slots[i].inst[0] != kNone && slots[i].inst[3] != kNone)
         {
            GpuInst gi = { instTotal, g.instCount, {}, gas.handle, sbt };
            for (int c = 0; c < 4; c++)
               gi.cols[c] = slots[i].inst[c];
            gpuInst.push_back(gi);
            instTotal += (size_t)g.instCount;
            continue;
         }
         auto add = [&](const Mat4& m, unsigned int id) {
            OptixInstance o = {};
            for (int r = 0; r < 3; r++)
               for (int c = 0; c < 4; c++)
                  o.transform[r * 4 + c] = m.m[c * 4 + r];
            o.instanceId = id;
            o.sbtOffset = sbt;
            o.visibilityMask = 255;
            o.flags = OPTIX_INSTANCE_FLAG_NONE;
            o.traversableHandle = gas.handle;
            hostInst.push_back(o);
         };
         if (!g.instCpu.empty())
            for (size_t k = 0; k < g.instCpu.size(); k++)
               add(g.instCpu[k], (unsigned int)k);
         else
            add(g.model, 0);
      }
      const size_t instCount = instTotal + hostInst.size();
      if (instCount == 0 || records.empty())
         return Fail("nothing to trace");
      const CUdeviceptr inst = mFrame.Get(instCount * sizeof(OptixInstance));
      if (inst == 0)
         return Fail("out of GPU memory for the instances");
      for (const GpuInst& gi : gpuInst)
      {
         CUdeviceptr dst = inst + gi.first * sizeof(OptixInstance);
         const float* c[4];
         for (int k = 0; k < 4; k++)
            c[k] = (const float*)(arena + gi.cols[k]);
         int count = gi.count;
         unsigned long long handle = gi.gas;
         unsigned int sbt = gi.sbt;
         void* args[] = { &dst, &c[0], &c[1], &c[2], &c[3], &count, &handle, &sbt };
         cuLaunchKernel(mBuildInstances, (unsigned)((count + 255) / 256), 1, 1, 256, 1, 1, 0, mStream, args, nullptr);
      }
      if (!hostInst.empty())
         cuMemcpyHtoDAsync(inst + instTotal * sizeof(OptixInstance), hostInst.data(),
                           hostInst.size() * sizeof(OptixInstance), mStream);

      // 4. The top-level structure over every instance.
      OptixBuildInput iin = {};
      iin.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
      iin.instanceArray.instances = inst;
      iin.instanceArray.numInstances = (unsigned)instCount;
      OptixAccelBuildOptions iopt = {};
      iopt.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
      iopt.operation = OPTIX_BUILD_OPERATION_BUILD;
      OptixAccelBufferSizes isz = {};
      if (!Check(optixAccelComputeMemoryUsage(mOx, &iopt, &iin, 1, &isz), "scene size"))
         return false;
      if (isz.outputSizeInBytes > mIasBytes)
      {
         if (mIas)
            cuMemFree(mIas);
         mIasBytes = Align(isz.outputSizeInBytes + isz.outputSizeInBytes / 4, 1u << 16);
         if (!Check(cuMemAlloc(&mIas, mIasBytes), "scene memory"))
         {
            mIas = 0;
            mIasBytes = 0;
            return false;
         }
      }
      OptixTraversableHandle scene = 0;
      const CUdeviceptr itemp = mFrame.Get(isz.tempSizeInBytes);
      if (itemp == 0)
         return Fail("out of GPU memory for the scene");
      if (!Check(optixAccelBuild(mOx, mStream, &iopt, &iin, 1, itemp, isz.tempSizeInBytes, mIas, mIasBytes, &scene,
                                 nullptr, 0),
                 "scene build"))
         return false;

      // 5. Launch.
      const CUdeviceptr hit = mFrame.Get(records.size() * sizeof(HitRecord));
      cuMemcpyHtoDAsync(hit, records.data(), records.size() * sizeof(HitRecord), mStream);
      OptixShaderBindingTable sbt = {};
      sbt.raygenRecord = mRaygenRec;
      sbt.missRecordBase = mMissRec;
      sbt.missRecordStrideInBytes = sizeof(SbtRecord<Empty>);
      sbt.missRecordCount = 1;
      sbt.hitgroupRecordBase = hit;
      sbt.hitgroupRecordStrideInBytes = sizeof(HitRecord);
      sbt.hitgroupRecordCount = (unsigned)records.size();

      RtxParams p = {};
      p.accum = (float4*)mAccum;
      p.average = (float4*)mAverage;
      p.albedo = (float4*)mAlbedo;
      p.normal = (float4*)mNormal;
      p.width = mW;
      p.height = mH;
      p.samples = mSamples;
      p.spp = std::max(1, f.spp);
      p.seed = mSeed++;
      p.bounces = std::clamp(f.bounces, 1, 16);
      const Mat4 inv = Mat4::Inverse(f.viewProj);
      std::memcpy(p.invViewProj, inv.m, sizeof(p.invViewProj));
      const float* v = f.view.m;
      const float viewRot[9] = { v[0], v[1], v[2], v[4], v[5], v[6], v[8], v[9], v[10] };
      std::memcpy(p.viewRot, viewRot, sizeof(viewRot));
      const float invRot[9] = { v[0], v[4], v[8], v[1], v[5], v[9], v[2], v[6], v[10] };
      std::memcpy(p.invViewRot, invRot, sizeof(invRot));
      p.eye = make_float3(f.eye[0], f.eye[1], f.eye[2]);
      p.panorama = f.panorama;
      p.domeAngle = f.domeAngle;
      p.domeTilt = f.domeTilt;
      p.scene = scene;
      p.ambient = make_float3(0, 0, 0);
      for (const Light& l : mLights)
      {
         const float3 c = ToLin3(l.color);
         if (l.type == 3)
         {
            p.ambient.x += c.x * l.intensity;
            p.ambient.y += c.y * l.intensity;
            p.ambient.z += c.z * l.intensity;
            continue;
         }
         if (p.lightCount >= RTX_MAX_LIGHTS)
            continue;
         RtxLight& o = p.lights[p.lightCount++];
         o.type = l.type;
         o.dir = make_float3(l.dir[0], l.dir[1], l.dir[2]);
         o.color = make_float3(c.x * l.intensity, c.y * l.intensity, c.z * l.intensity);
         o.spotCos0 = std::cos(l.spotAngle * 3.14159265f / 180.0f);
         o.spotCos1 = std::cos(l.spotAngle * 0.8f * 3.14159265f / 180.0f);
      }
      p.envTex = texObj(envTex);
      p.envRotation = mEnv.rotation;
      p.envIntensity = mEnv.intensity;
      p.sky = ToLin3(mEnv.sky);
      p.horizon = ToLin3(mEnv.horizon);
      p.ground = ToLin3(mEnv.ground);
      const CUdeviceptr dp = mFrame.Get(sizeof(RtxParams));
      cuMemcpyHtoDAsync(dp, &p, sizeof(p), mStream);
      cuEventRecord(mEvGeo, mStream);
      if (!Check(optixLaunch(mPipeline, mStream, dp, sizeof(RtxParams), &sbt, (unsigned)mW, (unsigned)mH, 1), "launch"))
         return false;
      mSamples += p.spp;
      cuEventRecord(mEvTrace, mStream);

      // 6. Denoise into the output pixel buffer (or copy the plain average).
      auto image = [this](CUdeviceptr data) {
         OptixImage2D im = {};
         im.data = data;
         im.width = (unsigned)mW;
         im.height = (unsigned)mH;
         im.rowStrideInBytes = (unsigned)(mW * 16);
         im.pixelStrideInBytes = 16;
         im.format = OPTIX_PIXEL_FORMAT_FLOAT4;
         return im;
      };
      bool denoised = false;
      // Auto: the fast filter while things move (the AI denoiser costs ~20 ms
      // at 1080p), the AI one once the image is accumulating on a still scene.
      const int mode = f.denoise == 0 ? (f.restart || mSamples <= p.spp ? 2 : 1) : f.denoise;
      if (mode == 2)
      {
         const int n = mW * mH;
         const unsigned blocks = (unsigned)((n + 255) / 256);
         CUdeviceptr avg = mAverage, alb = mAlbedo, nrm = mNormal, a = mTmp[0], b = mTmp[1];
         int count = n, w = mW, h = mH;
         void* d0[] = { &avg, &alb, &a, &count };
         cuLaunchKernel(mDemod, blocks, 1, 1, 256, 1, 1, 0, mStream, d0, nullptr);
         float sigma = 4.0f;
         for (int it = 0; it < 5; it++)
         {
            int step = 1 << it;
            void* d1[] = { &a, &b, &alb, &nrm, &w, &h, &step, &sigma };
            cuLaunchKernel(mAtrous, (unsigned)((w + 15) / 16), (unsigned)((h + 15) / 16), 1, 16, 16, 1, 0, mStream, d1,
                           nullptr);
            std::swap(a, b);
         }
         void* d2[] = { &a, &alb, &out, &count };
         cuLaunchKernel(mRemod, blocks, 1, 1, 256, 1, 1, 0, mStream, d2, nullptr);
         denoised = true;
      }
      else if (mode == 1 && mDenoiser)
      {
         OptixDenoiserLayer layer = {};
         layer.input = image(mAverage);
         layer.output = image(out);
         OptixDenoiserGuideLayer guide = {};
         guide.albedo = image(mAlbedo);
         guide.normal = image(mNormal);
         optixDenoiserComputeIntensity(mDenoiser, mStream, &layer.input, mIntensity, mDenScratch, mDenScratchBytes);
         OptixDenoiserParams dprm = {};
         dprm.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;
         dprm.hdrIntensity = mIntensity;
         dprm.blendFactor = 0.0f;
         denoised = optixDenoiserInvoke(mDenoiser, mStream, &dprm, mDenState, mDenStateBytes, &guide, &layer, 1, 0, 0,
                                        mDenScratch, mDenScratchBytes) == OPTIX_SUCCESS;
      }
      if (!denoised)
         cuMemcpyDtoDAsync(out, mAverage, (size_t)mW * mH * 16, mStream);
      cuEventRecord(mEvEnd, mStream);
      mTimed = true;
      mUpload = true;
      Evict();
      return true;
   }
}

std::unique_ptr<RtxBackend> CreateRtxBackend(std::string& why)
{
   auto b = std::make_unique<RtxOptix>();
   if (!b->Init(why))
      return nullptr;
   why.clear();
   return b;
}

bool RtxModuleBuilt()
{
   return true;
}
