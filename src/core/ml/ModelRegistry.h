#pragma once

// Shared ML model registry and single-resident ORT runtime for 2D View models
// (internal/plans/2d-view/ml-models/PLAN.md, Phase 2).
//
// Invariants:
// - At most ONE model family is resident in memory at a time (peak extra RSS <= 600 MB on 8 GB M2).
// - Idle models unload after 60 s without use (driven by Tick() from the main thread).
// - All heavy inference runs on a low-priority background worker with a latest-only request slot
//   and monotonic serial (never on UI, GL, or audio threads).
// - Packs are opt-in Extensions downloads verified by SHA-256 on first load; every tool keeps a
//   non-ML fallback when its pack is absent.

#include "core/tracking/OrtRuntime.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Ml
{
   enum class EpPref
   {
      CpuOnly,        // Default on macOS & Linux (Core ML EP is off unless proven fast + exact)
      PreferDirectMl, // Windows GPU (ORT_SEQUENTIAL + DisableMemPattern); CPU elsewhere
      PreferCoreMl,   // Only when a model is verified to compile in < 5 s and match CPU <= 1/255
   };

   struct ModelSpec
   {
      std::string id;                   // unique key, e.g. "modnet", "ppocr_det", "dfine_s"
      std::string name;                 // human-readable name for About Infinite
      std::string version;              // semantic version
      std::string pack;                 // Extensions pack id ("select", "ocr", "matte", "inpaint")
      std::string file;                 // relative path inside pack, e.g. "models/modnet.onnx"
      std::string sha256;               // expected lowercase sha256 hex (empty = skip hash check)
      uint64_t bytes = 0;               // expected file size in bytes (0 = query disk)
      std::string licenceId;            // SPDX id, e.g. "Apache-2.0", "MIT"
      std::string noticeText;           // attribution & provenance notice shown in About Infinite
      std::vector<int64_t> inputShape;  // static NCHW shape, e.g. { 1, 3, 512, 512 }
      EpPref epPrefs = EpPref::CpuOnly;
      std::string family;               // models sharing a family stay resident together (e.g. "ppocr")
   };

   struct InstalledModel
   {
      ModelSpec spec;
      std::string fullPath;
      uint64_t diskBytes = 0;
   };

   namespace ModelRegistry
   {
      // Registers a model spec (idempotent by id). Built-in 2D models are registered automatically.
      void Register(ModelSpec spec);
      const ModelSpec* Find(const std::string& id);
      std::vector<ModelSpec> List();

      // Models whose pack is installed and whose file exists on disk (for About Infinite notices).
      std::vector<InstalledModel> Installed();

      // True when the model's pack is installed, the model file exists, and an ORT runtime is present.
      bool Available(const std::string& id);
      // Human-readable reason why the model is unavailable ("" when available).
      std::string Missing(const std::string& id);
      // Full path to the model file on disk ("" if pack or file is missing).
      std::string ModelPath(const std::string& id);

      // Runs `fn(rt, model, err)` while holding the registry's runtime lock and keeping `id` resident.
      // Evicts any other loaded model family first, verifies SHA-256 once per file, and refreshes
      // the 60 s idle timer. Safe to call from worker threads.
      bool WithModel(const std::string& id,
                     const std::function<bool(Tracking::OrtRuntime&, Tracking::OrtModel*, std::string&)>& fn,
                     std::string& err);

      // Hook for legacy AiSelect sessions so loading SAM/Depth evicts ModelRegistry and vice versa.
      void SetExternalEvict(std::function<void()> fn);
      void EvictLocal();   // called by AiSelect before it opens SAM/Depth/MI-GAN
      void Tick(double nowSeconds);
      void Unload();

      // Diagnostics for tests.
      std::string ActiveFamily();
      bool VerifyFileSha256(const std::string& path, const std::string& expectedHex, std::string& err);
   } // namespace ModelRegistry

   // Low-priority single-thread worker with a latest-only job slot and monotonic serial.
   // Submitting a newer job replaces any queued (not yet started) job, matching RemoveBgNode.
   class LatestWorker
   {
   public:
      LatestWorker();
      ~LatestWorker();

      LatestWorker(const LatestWorker&) = delete;
      LatestWorker& operator=(const LatestWorker&) = delete;

      uint64_t Submit(std::function<void(uint64_t serial)> fn);
      bool Busy() const;
      uint64_t LatestSerial() const;
      uint64_t CompletedSerial() const;

   private:
      struct Impl;
      Impl* m_impl = nullptr;
   };
} // namespace Ml
