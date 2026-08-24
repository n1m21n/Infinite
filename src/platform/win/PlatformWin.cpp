// Windows implementation of the Platform facade's non-media pieces: native
// file dialogs, installed-font enumeration, glyph outlines for Text3DNode,
// executable paths, and graceful no-op/failure stand-ins for the macOS-only
// services (Syphon, Vision matting, AU plugins live in their own files).
//
// The macOS counterpart is src/platform/Platform.mm; every function here
// must keep the exact signature declared in ../Platform.h.

#include "../Platform.h"

#include "WinCommon.h"

#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
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

   bool SubjectMask(const std::vector<unsigned char>&, int, int,
                    MattingMode, std::vector<unsigned char>& outMask,
                    std::string& outError)
   {
      // Vision (person/subject segmentation) has no inbox Windows equivalent.
      // Fail softly so RemoveBgNode shows its error state instead of crashing.
      outMask.clear();
      outError = "background removal requires Apple's Vision framework (macOS build)";
      return false;
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

   // Syphon is macOS technology (IOSurface + Mach ports). On Windows the
   // nodes exist but report no servers and never connect; a future port
   // could map this surface onto Spout.
   namespace
   {
      struct SyphonStubHandle { int unused = 0; };
   }

   SyphonServerHandle* SyphonServerCreate(const std::string&)
   {
      return nullptr;
   }

   void SyphonServerUpdateName(SyphonServerHandle*, const std::string&) {}
   void SyphonServerPublish(SyphonServerHandle*, unsigned int, int, int, bool) {}
   bool SyphonServerHasClients(SyphonServerHandle*)
   {
      return false;
   }

   void SyphonServerDestroy(SyphonServerHandle*) {}

   std::vector<SyphonServerInfo> SyphonGetAvailableServers()
   {
      return {};
   }

   SyphonClientHandle* SyphonClientCreate()
   {
      return nullptr;
   }

   bool SyphonClientConnect(SyphonClientHandle*, const std::string&, const std::string&, const std::string&)
   {
      return false;
   }

   bool SyphonClientIsConnected(SyphonClientHandle*)
   {
      return false;
   }

   bool SyphonClientHasNewFrame(SyphonClientHandle*)
   {
      return false;
   }

   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle*, int&, int&)
   {
      return 0;
   }

   void SyphonClientDestroy(SyphonClientHandle*) {}

   // ---- Finder document opening ------------------------------------------

   void InitDocumentHandlingPreGlfw() {}
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
}
