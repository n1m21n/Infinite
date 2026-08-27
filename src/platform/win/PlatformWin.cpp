// Windows implementation of the Platform facade's non-media pieces: native
// file dialogs, installed-font enumeration, glyph outlines for Text3DNode,
// executable paths, and graceful no-op/failure stand-ins for the macOS-only
// services (Syphon, Vision matting, AU plugins live in their own files).
//
// The macOS counterpart is src/platform/Platform.mm; every function here
// must keep the exact signature declared in ../Platform.h.

#include "../Platform.h"

#include "WinCommon.h"

// For ConfigureOutputWindow/ReassertOutputWindowTopmost's glfwGetWin32Window
// call - this file otherwise has no reason to see GLFW at all. Must come
// after WinCommon.h, which already defines WIN32_LEAN_AND_MEAN/NOMINMAX.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace
{
   // ---- dialogs -----------------------------------------------------------

   struct FilterSpec
   {
      const wchar_t* label;
      const wchar_t* pattern;
   };

   // Runs an IFileDialog open panel. `folder` switches to folder mode.
   // Returns "" when cancelled or on failure (failure also fills outError).
   std::string RunOpenDialog(const wchar_t* title, const std::vector<FilterSpec>& filters,
                             bool folder, const std::string& initialDir, std::string& outError)
   {
      outError.clear();

      HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      bool mustUninit = SUCCEEDED(hrInit);

      std::string result;
      ComPtr<IFileOpenDialog> dialog;
      HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog));
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("CoCreateInstance(FileOpenDialog)", hr);
      }
      else
      {
         DWORD options;
         dialog->GetOptions(&options);
         options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
         if (folder)
            options |= FOS_PICKFOLDERS;
         else
            options |= FOS_FILEMUSTEXIST;
         dialog->SetOptions(options);
         dialog->SetTitle(title);

         if (!filters.empty())
         {
            std::vector<COMDLG_FILTERSPEC> specs;
            specs.reserve(filters.size() + 1);
            for (const FilterSpec& f : filters)
               specs.push_back({ f.label, f.pattern });
            specs.push_back({ L"All files", L"*.*" });
            dialog->SetFileTypes((UINT)specs.size(), specs.data());
            dialog->SetFileTypeIndex(1);
         }

         if (!initialDir.empty())
         {
            ComPtr<IShellItem> folderItem;
            const std::wstring wide = WinCommon::Utf8ToWide(initialDir);
            if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr,
                                                      IID_PPV_ARGS(&folderItem))))
               dialog->SetFolder(folderItem.Get());
         }

         hr = dialog->Show(nullptr);
         if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
         {
            // Cancelled - not an error, just no path.
         }
         else if (FAILED(hr))
         {
            outError = WinCommon::HrToString("IFileOpenDialog::Show", hr);
         }
         else
         {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
            {
               PWSTR path = nullptr;
               if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr)
               {
                  result = WinCommon::WideToUtf8(path);
                  CoTaskMemFree(path);
               }
            }
         }
      }

      if (mustUninit)
         CoUninitialize();
      return result;
   }

   std::string RunSaveDialog(const wchar_t* title, const std::vector<FilterSpec>& filters,
                             const std::string& suggestedName, std::string& outError)
   {
      outError.clear();

      HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      bool mustUninit = SUCCEEDED(hrInit);

      std::string result;
      ComPtr<IFileSaveDialog> dialog;
      HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog));
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("CoCreateInstance(FileSaveDialog)", hr);
      }
      else
      {
         DWORD options;
         dialog->GetOptions(&options);
         options |= FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
         dialog->SetOptions(options);
         dialog->SetTitle(title);

         if (!filters.empty())
         {
            std::vector<COMDLG_FILTERSPEC> specs;
            specs.reserve(filters.size() + 1);
            for (const FilterSpec& f : filters)
               specs.push_back({ f.label, f.pattern });
            specs.push_back({ L"All files", L"*.*" });
            dialog->SetFileTypes((UINT)specs.size(), specs.data());
            dialog->SetFileTypeIndex(1);
         }

         if (!suggestedName.empty())
         {
            const std::wstring wide = WinCommon::Utf8ToWide(suggestedName);
            dialog->SetFileName(wide.c_str());
         }

         hr = dialog->Show(nullptr);
         if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
         {
            // cancelled
         }
         else if (FAILED(hr))
         {
            outError = WinCommon::HrToString("IFileSaveDialog::Show", hr);
         }
         else
         {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr)
            {
               PWSTR path = nullptr;
               if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr)
               {
                  result = WinCommon::WideToUtf8(path);
                  CoTaskMemFree(path);
               }
            }
         }
      }

      if (mustUninit)
         CoUninitialize();
      return result;
   }

   // ---- fonts -------------------------------------------------------------

   int CALLBACK FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lParam)
   {
      auto* names = reinterpret_cast<std::vector<std::string>*>(lParam);
      if ((lf->lfPitchAndFamily & 0x70) == VARIABLE_PITCH || true) // keep all families
      {
         if (lf->lfFaceName[0] != L'@') // skip vertical-metric duplicates
            names->push_back(WinCommon::WideToUtf8(lf->lfFaceName));
      }
      return 1;
   }

   // FIXED (16.16) -> float
   inline float FixedToFloat(FIXED f)
   {
      return (float)f.value + (float)f.fract / 65536.0f;
   }

   // Closes a WinHTTP handle on every exit path (including early returns on
   // error) without goto-cleanup. HttpGet() opens up to four of these
   // (session/connect/request, plus the implicit ones WinHttp owns) and each
   // one leaks a handle if a single error path forgets to close it.
   struct WinHttpHandleGuard
   {
      HINTERNET handle = nullptr;
      WinHttpHandleGuard() = default;
      explicit WinHttpHandleGuard(HINTERNET h) : handle(h) {}
      ~WinHttpHandleGuard() { if (handle) WinHttpCloseHandle(handle); }
      WinHttpHandleGuard(const WinHttpHandleGuard&) = delete;
      WinHttpHandleGuard& operator=(const WinHttpHandleGuard&) = delete;
      operator HINTERNET() const { return handle; }
   };
}

namespace Platform
{
   void PreventAppNap()
   {
      // macOS-only concern (App Nap throttling timer sources). Windows has no
      // equivalent power-throttling behavior that affects GLFW timers.
   }

   std::string OpenImageDialog()
   {
      std::string err;
      return RunOpenDialog(L"Choose Image",
                           {
                              { L"Image files", L"*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.tif;*.tiff;*.tga;*.webp;*.hdr;*.pic;*.ppm;*.pgm" },
                              { L"All supported", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp" },
                           },
                           false, std::string(), err);
   }

   std::string OpenHdrDialog()
   {
      std::string err;
      return RunOpenDialog(L"Choose HDR Environment",
                           {
                              { L"HDR images", L"*.hdr;*.exr" },
                              { L"Radiance HDR", L"*.hdr" },
                              { L"OpenEXR", L"*.exr" },
                           },
                           false, std::string(), err);
   }

   std::string OpenModelDialog()
   {
      std::string err;
      return RunOpenDialog(L"Choose Model",
                           {
                              { L"3D models", L"*.obj;*.ply;*.stl" },
                              { L"Wavefront OBJ", L"*.obj" },
                              { L"Stanford PLY", L"*.ply" },
                              { L"Stereolithography STL", L"*.stl" },
                           },
                           false, std::string(), err);
   }

   std::string OpenPatchDialog()
   {
      std::string err;
      return RunOpenDialog(L"Open Patch",
                           {
                              { L"Infinite patches", L"*.inf;*.infinite" },
                           },
                           false, std::string(), err);
   }

   std::string SavePatchDialog(const std::string& suggestedName)
   {
      std::string err;
      return RunSaveDialog(L"Save Patch",
                           {
                              { L"Infinite patch", L"*.infinite" },
                           },
                           suggestedName, err);
   }

   std::string OpenVideoDialog()
   {
      std::string err;
      return RunOpenDialog(L"Choose Video",
                           {
                              { L"Video files", L"*.mp4;*.mov;*.m4v;*.avi;*.mkv;*.webm;*.wmv" },
                           },
                           false, std::string(), err);
   }

   std::string OpenAudioDialog()
   {
      std::string err;
      return RunOpenDialog(L"Choose Audio File",
                           {
                              { L"Audio files", L"*.wav;*.aif;*.aiff;*.mp3;*.flac" },
                              { L"WAV", L"*.wav" },
                              { L"AIFF", L"*.aif;*.aiff" },
                              { L"MP3", L"*.mp3" },
                              { L"FLAC", L"*.flac" },
                           },
                           false, std::string(), err);
   }

   std::string OpenFolderDialog(const char* title, const std::string& initialDir)
   {
      std::string err;
      const std::wstring wtitle = WinCommon::Utf8ToWide(title ? title : "Add sample folder");
      return RunOpenDialog(wtitle.c_str(), {}, true, initialDir, err);
   }

   const std::vector<std::string>& AvailableFontFamilies()
   {
      static std::vector<std::string> sFonts;
      static bool sLoaded = false;
      if (sLoaded)
         return sFonts;
      sLoaded = true;

      HDC hdc = GetDC(nullptr);
      if (hdc != nullptr)
      {
         LOGFONTW lf {};
         lf.lfCharSet = DEFAULT_CHARSET;
         EnumFontFamiliesExW(hdc, &lf, FontEnumProc, (LPARAM)&sFonts, 0);
         ReleaseDC(nullptr, hdc);
      }

      std::sort(sFonts.begin(), sFonts.end());
      sFonts.erase(std::unique(sFonts.begin(), sFonts.end()), sFonts.end());
      if (sFonts.empty())
         sFonts.push_back("Arial");
      return sFonts;
   }

   bool GetTextOutlines(const std::string& text, const std::string& fontName,
                        float letterSpacing, std::vector<TextContour>& outContours,
                        std::string& outError)
   {
      outContours.clear();
      outError.clear();

      if (text.empty())
      {
         outError = "empty text";
         return false;
      }

      // Negative height = em size (per-font-unit coordinate space, which is
      // what GGO_NATIVE reports regardless of the value chosen - but a larger
      // em keeps FIXED quantization noise negligible).
      const int kEm = 4096;
      const std::wstring face = WinCommon::Utf8ToWide(fontName.empty() ? "Arial" : fontName);

      HDC hdc = GetDC(nullptr);
      HFONT font = CreateFontW(-kEm, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face.c_str());
      if (font == nullptr)
      {
         ReleaseDC(nullptr, hdc);
         outError = "could not create font";
         return false;
      }

      HGDIOBJ old = SelectObject(hdc, font);

      // UTF-16 code units of the request text.
      const std::wstring wide = WinCommon::Utf8ToWide(text);

      bool ok = true;
      float penX = 0.0f;

      // First pass: measure cap height from 'H' so the output can be
      // normalised the way the CoreGraphics version was (cap height ~ 1).
      // Identity transform. FIXED is { WORD fract; SHORT value; }, so each
      // pair below is (fract=0, value=1-or-0) - 16.16 fixed point.
      const FIXED fxIdentity = { 0, 1 };
      const FIXED fxZero = { 0, 0 };
      const MAT2 mat2 = { fxIdentity, fxZero, fxZero, fxIdentity };
      float capHeight = 0.0f;
      {
         GLYPHMETRICS metrics {};
         const DWORD size = GetGlyphOutlineW(hdc, L'H', GGO_NATIVE | GGO_GLYPH_INDEX,
                                             &metrics, 0, nullptr, &mat2);
         if (size > 0 && size != GDI_ERROR)
         {
            std::vector<char> buf(size);
            if (GetGlyphOutlineW(hdc, L'H', GGO_NATIVE | GGO_GLYPH_INDEX, &metrics,
                                 size, buf.data(), &mat2) != GDI_ERROR)
            {
               // Walk just for max-y.
               const char* ptr = buf.data();
               const char* end = buf.data() + size;
               while (ptr + sizeof(TTPOLYGONHEADER) <= end)
               {
                  const TTPOLYGONHEADER* h = reinterpret_cast<const TTPOLYGONHEADER*>(ptr);
                  capHeight = std::max(capHeight, FixedToFloat(h->pfxStart.y));
                  const char* p2 = ptr + sizeof(TTPOLYGONHEADER);
                  const char* e2 = ptr + h->cb;
                  while (p2 + sizeof(TTPOLYCURVE) <= e2)
                  {
                     const TTPOLYCURVE* c = reinterpret_cast<const TTPOLYCURVE*>(p2);
                     for (WORD i = 0; i < c->cpfx; i++)
                        capHeight = std::max(capHeight, FixedToFloat(c->apfx[i].y));
                     p2 += offsetof(TTPOLYCURVE, apfx) + (size_t)c->cpfx * sizeof(POINTFX);
                  }
                  ptr += h->cb;
               }
            }
         }
      }
      if (capHeight <= 0.0f)
         capHeight = (float)kEm * 0.7f; // sane fallback

      const float scale = 1.0f / capHeight;

      for (size_t i = 0; i < wide.size() && ok; i++)
      {
         const wchar_t wc = wide[i];
         if (wc == L'\r' || wc == L'\n')
            continue; // single-line outlines only, same as the CoreText path

         GLYPHMETRICS metrics {};
         DWORD size = GetGlyphOutlineW(hdc, wc, GGO_NATIVE, &metrics, 0, nullptr, &mat2);
         if (size == 0 || size == GDI_ERROR)
         {
            penX += (float)kEm * 0.3f; // advance placeholder for unmapped chars
            continue;
         }

         std::vector<char> buf(size);
         if (GetGlyphOutlineW(hdc, wc, GGO_NATIVE, &metrics, size, buf.data(), &mat2) == GDI_ERROR)
         {
            ok = false;
            outError = "GetGlyphOutline failed";
            break;
         }

         const char* ptr = buf.data();
         const char* end = buf.data() + size;
         while (ptr + sizeof(TTPOLYGONHEADER) <= end)
         {
            const TTPOLYGONHEADER* header = reinterpret_cast<const TTPOLYGONHEADER*>(ptr);
            outContours.push_back(TextContour());

            // Flatten with pen offset applied manually.
            const char* p2 = reinterpret_cast<const char*>(header) + sizeof(TTPOLYGONHEADER);
            const char* e2 = reinterpret_cast<const char*>(header) + header->cb;
            float curX = FixedToFloat(header->pfxStart.x) + penX;
            float curY = FixedToFloat(header->pfxStart.y);
            outContours.back().points.push_back(curX * scale);
            outContours.back().points.push_back(curY * scale);

            while (p2 < e2)
            {
               const TTPOLYCURVE* curve = reinterpret_cast<const TTPOLYCURVE*>(p2);
               const POINTFX* pts = curve->apfx;
               const WORD count = curve->cpfx;

               if (curve->wType == TT_PRIM_LINE)
               {
                  for (WORD k = 0; k < count; k++)
                  {
                     curX = FixedToFloat(pts[k].x) + penX;
                     curY = FixedToFloat(pts[k].y);
                     outContours.back().points.push_back(curX * scale);
                     outContours.back().points.push_back(curY * scale);
                  }
               }
               else if (curve->wType == TT_PRIM_QSPLINE)
               {
                  for (WORD k = 0; k + 1 < count; k++)
                  {
                     const float cx = FixedToFloat(pts[k].x) + penX;
                     const float cy = FixedToFloat(pts[k].y);
                     const bool lastSeg = (k + 2 == count);
                     const float ex = lastSeg ? FixedToFloat(pts[k + 1].x) + penX
                                              : 0.5f * (FixedToFloat(pts[k + 1].x) + FixedToFloat(pts[k + 2].x)) + penX;
                     const float ey = lastSeg ? FixedToFloat(pts[k + 1].y)
                                              : 0.5f * (FixedToFloat(pts[k + 1].y) + FixedToFloat(pts[k + 2].y));

                     const float polyLen =
                        std::sqrt((cx - curX) * (cx - curX) + (cy - curY) * (cy - curY)) +
                        std::sqrt((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy));
                     const int steps = std::clamp((int)(polyLen * 0.25f * scale), 2, 24);
                     for (int s = 1; s <= steps; s++)
                     {
                        const float t = (float)s / (float)(steps + 1);
                        const float mt = 1.0f - t;
                        outContours.back().points.push_back(
                           (mt * mt * curX + 2.0f * mt * t * cx + t * t * ex) * scale);
                        outContours.back().points.push_back(
                           (mt * mt * curY + 2.0f * mt * t * cy + t * t * ey) * scale);
                     }
                     curX = ex;
                     curY = ey;
                  }
                  curX = FixedToFloat(pts[count - 1].x) + penX;
                  curY = FixedToFloat(pts[count - 1].y);
                  outContours.back().points.push_back(curX * scale);
                  outContours.back().points.push_back(curY * scale);
               }

               p2 += offsetof(TTPOLYCURVE, apfx) + (size_t)curve->cpfx * sizeof(POINTFX);
            }

            ptr += header->cb;
         }

         penX += (float)(metrics.gmCellIncX) + letterSpacing * (float)kEm;
      }

      SelectObject(hdc, old);
      DeleteObject(font);
      ReleaseDC(nullptr, hdc);

      return ok && !outContours.empty();
   }

   // ---- executable paths --------------------------------------------------

   std::string ExecutablePath()
   {
      wchar_t path[MAX_PATH];
      const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
      if (len == 0 || len >= MAX_PATH)
         return {};
      return WinCommon::WideToUtf8(std::wstring(path, len));
   }

   std::string ScannerExecutablePath()
   {
      // Lives next to the app executable when built with INFINITE_ENABLE_VST3.
      const std::string exe = ExecutablePath();
      const size_t slash = exe.find_last_of("/\\");
      return slash == std::string::npos ? std::string() : exe.substr(0, slash + 1) + "infinite-vst3-scanner.exe";
   }

   void SuppressAppUIForScanChild()
   {
      // macOS-only Dock-icon dance; the Windows scan child is a console-less
      // process with no window station presence of its own.
   }

   // ---- background removal ------------------------------------------------
   //
   // Windows has no inbox equivalent to Vision, so this runs u2netp (a small
   // U^2-Net salient-object model, see assets/models/NOTICE.txt) through ONNX
   // Runtime with the DirectML execution provider - GPU-accelerated on any
   // DX12 GPU, with ORT's own CPU EP as the automatic fallback if DML
   // registration or execution fails. Person mode maps to the same model:
   // u2netp is a general salient-object detector, not a person-only model,
   // but a dedicated Windows person-segmentation model is more machinery than
   // this feature is worth - see RemoveBgNode's mode names for how this is
   // surfaced to the user.

   // Holds the one-time-constructed session and everything needed to run it.
   // Built once (see EnsureOrtSession below) and reused for every call -
   // constructing a session (loading + optimizing the model graph) costs far
   // more than a single inference and there is no reason to repeat it.
   struct OrtMattingSession
   {
      Ort::Env env{ ORT_LOGGING_LEVEL_WARNING, "Infinite" };
      std::unique_ptr<Ort::Session> session;
      std::string inputName;
      std::string outputName;
      int inputW = 320;
      int inputH = 320;
      std::string error; // non-empty if construction failed; session stays null
   };

   OrtMattingSession& EnsureOrtSession()
   {
      static OrtMattingSession* holder = nullptr;
      static std::once_flag once;
      std::call_once(once, [] {
         holder = new OrtMattingSession();

         const std::string exe = ExecutablePath();
         const size_t slash = exe.find_last_of("/\\");
         if (slash == std::string::npos)
         {
            holder->error = "could not locate the app's own executable path";
            return;
         }
         const std::wstring modelPath =
            WinCommon::Utf8ToWide(exe.substr(0, slash + 1) + "assets\\models\\u2netp.onnx");

         try
         {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(1);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // DirectML needs sequential execution; a failed registration
            // (missing/old GPU driver, no DX12) is not fatal - the session
            // still runs on ORT's built-in CPU EP, just slower.
            try
            {
               options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
               options.DisableMemPattern();
               Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
            }
            catch (const Ort::Exception&)
            {
               // Fall through and run on CPU.
            }

            holder->session = std::make_unique<Ort::Session>(holder->env, modelPath.c_str(), options);

            Ort::AllocatorWithDefaultOptions allocator;
            Ort::AllocatedStringPtr inName = holder->session->GetInputNameAllocated(0, allocator);
            Ort::AllocatedStringPtr outName = holder->session->GetOutputNameAllocated(0, allocator);
            holder->inputName = inName.get();
            holder->outputName = outName.get();

            const Ort::TypeInfo inputInfo = holder->session->GetInputTypeInfo(0);
            const std::vector<int64_t> shape = inputInfo.GetTensorTypeAndShapeInfo().GetShape();
            // NCHW; only trust H/W from the model if they're fixed (not -1).
            if (shape.size() == 4 && shape[2] > 0 && shape[3] > 0)
            {
               holder->inputH = (int)shape[2];
               holder->inputW = (int)shape[3];
            }
         }
         catch (const Ort::Exception& e)
         {
            holder->session.reset();
            holder->error = std::string("could not load background removal model: ") + e.what();
         }
      });
      return *holder;
   }

   // Bilinear-resamples RGBA (dropping alpha) into a planar CHW float tensor,
   // normalized the way U^2-Net's own preprocessing does: scaled to [0,1]
   // then per-channel mean/std (ImageNet statistics, as used by the reference
   // training/inference code this model was exported from).
   void ResizeAndNormalize(const unsigned char* rgba, int srcW, int srcH, bool srcBottomUp,
                           float* chw, int dstW, int dstH)
   {
      static const float kMean[3] = { 0.485f, 0.456f, 0.406f };
      static const float kStd[3] = { 0.229f, 0.224f, 0.225f };
      const size_t srcStride = (size_t)srcW * 4;
      const size_t planeSize = (size_t)dstW * dstH;

      for (int y = 0; y < dstH; y++)
      {
         const float sy = (dstH > 1) ? ((float)y + 0.5f) * srcH / dstH - 0.5f : 0.0f;
         const int sy0 = std::clamp((int)std::floor(sy), 0, srcH - 1);
         const int sy1 = std::clamp(sy0 + 1, 0, srcH - 1);
         const float fy = std::clamp(sy - sy0, 0.0f, 1.0f);
         const int ry0 = srcBottomUp ? (srcH - 1 - sy0) : sy0;
         const int ry1 = srcBottomUp ? (srcH - 1 - sy1) : sy1;

         for (int x = 0; x < dstW; x++)
         {
            const float sx = (dstW > 1) ? ((float)x + 0.5f) * srcW / dstW - 0.5f : 0.0f;
            const int sx0 = std::clamp((int)std::floor(sx), 0, srcW - 1);
            const int sx1 = std::clamp(sx0 + 1, 0, srcW - 1);
            const float fx = std::clamp(sx - sx0, 0.0f, 1.0f);

            for (int c = 0; c < 3; c++)
            {
               const float p00 = rgba[ry0 * srcStride + sx0 * 4 + c];
               const float p10 = rgba[ry0 * srcStride + sx1 * 4 + c];
               const float p01 = rgba[ry1 * srcStride + sx0 * 4 + c];
               const float p11 = rgba[ry1 * srcStride + sx1 * 4 + c];
               const float top = p00 + (p10 - p00) * fx;
               const float bot = p01 + (p11 - p01) * fx;
               const float value = (top + (bot - top) * fy) / 255.0f;
               chw[c * planeSize + (size_t)y * dstW + x] = (value - kMean[c]) / kStd[c];
            }
         }
      }
   }

   bool SubjectMask(const std::vector<unsigned char>& rgbaPixels, int width, int height,
                    MattingMode /*mode*/, std::vector<unsigned char>& outMask,
                    std::string& outError)
   {
      outMask.clear();

      if (width <= 0 || height <= 0 || rgbaPixels.size() < (size_t)width * height * 4)
      {
         outError = "bad image";
         return false;
      }

      OrtMattingSession& ort = EnsureOrtSession();
      if (!ort.session)
      {
         outError = ort.error.empty() ? "background removal model unavailable" : ort.error;
         return false;
      }

      try
      {
         std::vector<float> input((size_t)3 * ort.inputW * ort.inputH);
         // rgbaPixels arrives bottom-up (GL order), same as the macOS path.
         ResizeAndNormalize(rgbaPixels.data(), width, height, /*srcBottomUp=*/true,
                            input.data(), ort.inputW, ort.inputH);

         Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
         const int64_t inputShape[4] = { 1, 3, ort.inputH, ort.inputW };
         Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, input.data(), input.size(), inputShape, 4);

         const char* inputNames[] = { ort.inputName.c_str() };
         const char* outputNames[] = { ort.outputName.c_str() };
         auto outputs = ort.session->Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1,
                                         outputNames, 1);

         const float* pred = outputs[0].GetTensorData<float>();
         const size_t predCount = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
         const size_t maskPlane = (size_t)ort.inputW * ort.inputH;
         if (predCount < maskPlane)
         {
            outError = "background removal model returned an unexpected output shape";
            return false;
         }

         // Reference U^2-Net postprocessing: min-max normalize the saliency
         // map to [0,1] before turning it into a mask image.
         float lo = pred[0], hi = pred[0];
         for (size_t i = 0; i < maskPlane; i++)
         {
            lo = std::min(lo, pred[i]);
            hi = std::max(hi, pred[i]);
         }
         const float range = (hi - lo) > 1e-6f ? (hi - lo) : 1.0f;

         std::vector<unsigned char> modelMask(maskPlane);
         for (size_t i = 0; i < maskPlane; i++)
         {
            const float v = (pred[i] - lo) / range;
            modelMask[i] = (unsigned char)std::clamp(v * 255.0f, 0.0f, 255.0f);
         }

         // Rescale (nearest) to the requested size and flip to GL order, same
         // as the macOS/Vision path.
         outMask.assign((size_t)width * height, 0);
         for (int y = 0; y < height; y++)
         {
            const int sy = std::min(ort.inputH - 1, y * ort.inputH / height);
            unsigned char* dstRow = &outMask[(size_t)(height - 1 - y) * width];
            const unsigned char* srcRow = &modelMask[(size_t)sy * ort.inputW];
            for (int x = 0; x < width; x++)
               dstRow[x] = srcRow[std::min(ort.inputW - 1, x * ort.inputW / width)];
         }

         outError.clear();
         return true;
      }
      catch (const Ort::Exception& e)
      {
         outMask.clear();
         outError = std::string("background removal failed: ") + e.what();
         return false;
      }
   }

   // ---- audio synthesis spike (throwaway feasibility probe) ---------------

   bool AudioSpikeStart(std::string&)
   {
      return false; // P0 throwaway, never wired into the product UI
   }

   void AudioSpikeStop() {}

   AudioSpikeStats AudioSpikeGetStats()
   {
      return {};
   }

   // ---- device change recovery --------------------------------------------

   bool AudioWillSleep()
   {
      return false; // NSWorkspace sleep notifications are macOS-only
   }

   bool AudioDidWake()
   {
      return false;
   }

   // ---- Syphon inter-app video sharing ------------------------------------

   // The real implementation (backed by Spout2) lives in
   // PlatformWinSyphon.cpp, not here: it needs SpoutGL's headers, which pull
   // in DirectX11 and the legacy <GL/gl.h>, and this file is also compiled
   // into the infinite-vst3-scanner helper target, which has no GL/DX
   // context and must stay a lightweight console-only process. Splitting
   // the Syphon/Spout code into its own translation unit - added to
   // WIN32_SOURCES for the main Infinite target only - keeps the scanner
   // build untouched by it.

   // ---- Finder document opening ------------------------------------------

   void InitDocumentHandlingPreGlfw()
   {
      // Explicit AppUserModelID ensures Windows Taskbar groups windows under
      // the Infinite application and displays the correct pinned taskbar icon.
      SetCurrentProcessExplicitAppUserModelID(L"Infinite.Synthesizer");
   }
   void InitDocumentHandlingPostGlfw() {}

   bool PollPendingOpenFile(std::string&)
   {
      // Drag-and-drop onto the window goes through glfwSetDropCallback on
      // all platforms; double-click-to-open associations were implemented
      // with Apple Events and have no Windows counterpart wired up yet.
      return false;
   }

   // ---- networking (update checker) ----------------------------------------

   void OpenExternalUrl(const std::string& url)
   {
      if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
         return;

      const std::wstring wideUrl = WinCommon::Utf8ToWide(url);
      ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
   }

   bool HttpGet(const std::string& url, const std::string& userAgent,
                std::string& outBody, std::string& outError,
                int timeoutSeconds)
   {
      outBody.clear();
      outError.clear();

      const bool isHttps = url.rfind("https://", 0) == 0;
      if (!isHttps && url.rfind("http://", 0) != 0)
      {
         outError = "url must be http(s)";
         return false;
      }

      URL_COMPONENTS parts;
      ZeroMemory(&parts, sizeof(parts));
      parts.dwStructSize = sizeof(parts);
      wchar_t hostBuf[256] = {};
      wchar_t pathBuf[2048] = {};
      parts.lpszHostName = hostBuf;
      parts.dwHostNameLength = (DWORD)(sizeof(hostBuf) / sizeof(hostBuf[0]));
      parts.lpszUrlPath = pathBuf;
      parts.dwUrlPathLength = (DWORD)(sizeof(pathBuf) / sizeof(pathBuf[0]));

      const std::wstring wideUrl = WinCommon::Utf8ToWide(url);
      if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
      {
         outError = "malformed url";
         return false;
      }

      WinHttpHandleGuard session(WinHttpOpen(WinCommon::Utf8ToWide(userAgent).c_str(),
         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
      if (!session.handle)
      {
         outError = "WinHttpOpen failed";
         return false;
      }

      const int timeoutMs = timeoutSeconds * 1000;
      WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

      WinHttpHandleGuard connect(WinHttpConnect(session, parts.lpszHostName, parts.nPort, 0));
      if (!connect.handle)
      {
         outError = "WinHttpConnect failed";
         return false;
      }

      WinHttpHandleGuard request(WinHttpOpenRequest(connect, L"GET", parts.lpszUrlPath,
         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
         isHttps ? WINHTTP_FLAG_SECURE : 0));
      if (!request.handle)
      {
         outError = "WinHttpOpenRequest failed";
         return false;
      }

      if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
          !WinHttpReceiveResponse(request, nullptr))
      {
         outError = "request failed (network or TLS error)";
         return false;
      }

      DWORD statusCode = 0;
      DWORD statusSize = sizeof(statusCode);
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
         WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
      if (statusCode < 200 || statusCode >= 300)
      {
         outError = "http status " + std::to_string((long)statusCode);
         return false;
      }

      static constexpr size_t kMaxBodyBytes = 1 * 1024 * 1024;
      std::string body;
      for (;;)
      {
         DWORD available = 0;
         if (!WinHttpQueryDataAvailable(request, &available))
         {
            outError = "WinHttpQueryDataAvailable failed";
            return false;
         }
         if (available == 0)
            break;

         if (body.size() + available > kMaxBodyBytes)
         {
            outError = "response exceeded size cap";
            return false;
         }

         size_t oldSize = body.size();
         body.resize(oldSize + available);
         DWORD read = 0;
         if (!WinHttpReadData(request, &body[oldSize], available, &read))
         {
            outError = "WinHttpReadData failed";
            return false;
         }
         body.resize(oldSize + read);
         if (read == 0)
            break;
      }

      outBody = std::move(body);
      return true;
   }

   // ---- output/projector window --------------------------------------------
   // See Platform.h's comment on why this exists only on Windows: there is no
   // OS-level "make this window fullscreen" affordance here, so main.cpp
   // fakes one with a borderless, always-on-top window instead of exclusive
   // GLFW fullscreen (which would auto-iconify / drop behind the editor the
   // moment focus moves to the other display).
   void ConfigureOutputWindow(GLFWwindow* window, bool borderless, bool topmost, bool hideCursor)
   {
      HWND hwnd = glfwGetWin32Window(window);
      if (hwnd == nullptr)
         return;

      LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (borderless)
         style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
      else
         style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
      SetWindowLongPtrW(hwnd, GWL_STYLE, style);

      SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

      glfwSetInputMode(window, GLFW_CURSOR, hideCursor ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
   }

   // Windows will silently demote a HWND_TOPMOST window in real situations -
   // another app going topmost, a UAC prompt, display hot-plug, a resolution
   // change - so main.cpp's projector loop calls this on a throttle (not
   // every frame) while a projector window is fullscreen, to keep it pinned
   // above everything else.
   void ReassertOutputWindowTopmost(GLFWwindow* window)
   {
      HWND hwnd = glfwGetWin32Window(window);
      if (hwnd == nullptr)
         return;

      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
   }

   void SetWindowIconFromResource(GLFWwindow* window)
   {
      HWND hwnd = glfwGetWin32Window(window);
      if (hwnd == nullptr)
         return;

      HINSTANCE hInstance = GetModuleHandleW(nullptr);
      HICON hIconBig = (HICON)LoadImageW(hInstance, L"GLFW_ICON", IMAGE_ICON,
                                         GetSystemMetrics(SM_CXICON),
                                         GetSystemMetrics(SM_CYICON),
                                         LR_SHARED);
      HICON hIconSmall = (HICON)LoadImageW(hInstance, L"GLFW_ICON", IMAGE_ICON,
                                           GetSystemMetrics(SM_CXSMICON),
                                           GetSystemMetrics(SM_CYSMICON),
                                           LR_SHARED);
      if (hIconBig != nullptr)
         SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
      if (hIconSmall != nullptr)
         SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
   }
}
