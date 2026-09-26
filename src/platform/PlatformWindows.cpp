#include "Platform.h"


#include "platform/OpenGLHeaders.h"
#include "audio/AudioFileWriter.h"
#include "core/RuntimeLog.h"

#include <windows.h>
#include <timeapi.h>
#include <avrt.h>
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <dshow.h>
#include <dxgi1_6.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#ifndef INFINITE_HAS_WINDOWS_ML
#define INFINITE_HAS_WINDOWS_ML 0
#endif

#if INFINITE_HAS_WINDOWS_ML
#include <onnxruntime_cxx_api.h>

// Windows ML bundles the DirectML execution provider. Its factory is exported
// by onnxruntime.dll but intentionally lives outside the stable ORT API table.
extern "C"
{
   ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_DML,
                  _In_ OrtSessionOptions* options, int deviceId);
}
#endif

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#if INFINITE_ENABLE_SPOUT
#include <SpoutSender.h>
#include <SpoutReceiver.h>
#endif

// Hybrid-GPU laptops otherwise let Windows choose the integrated adapter for
// an unsigned custom executable. These documented vendor exports request the
// high-performance adapter before GLFW creates the first OpenGL context.
extern "C"
{
   __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
   __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Platform
{
   void EnsureJuceInitialised();

   bool ReadGpuStats(GpuStats& out)
   {
      using NvmlReturn = int;
      using NvmlDevice = void*;
      struct NvmlUtilization { unsigned int gpu; unsigned int memory; };
      struct NvmlMemory { unsigned long long total; unsigned long long free; unsigned long long used; };

      struct NvmlApi
      {
         HMODULE module = nullptr;
         NvmlReturn (*init)() = nullptr;
         NvmlReturn (*getCount)(unsigned int*) = nullptr;
         NvmlReturn (*getHandle)(unsigned int, NvmlDevice*) = nullptr;
         NvmlReturn (*getUtilization)(NvmlDevice, NvmlUtilization*) = nullptr;
         NvmlReturn (*getMemory)(NvmlDevice, NvmlMemory*) = nullptr;
         NvmlReturn (*getName)(NvmlDevice, char*, unsigned int) = nullptr;
         bool attempted = false;
         int selected = -1;
         GpuStats cached;
         std::chrono::steady_clock::time_point lastPoll {};
      };
      static NvmlApi api;

      if (!api.attempted)
      {
         api.attempted = true;
         api.module = LoadLibraryW(L"nvml.dll");
         if (api.module != nullptr)
         {
            api.init = reinterpret_cast<NvmlReturn (*)()>(GetProcAddress(api.module, "nvmlInit_v2"));
            api.getCount = reinterpret_cast<NvmlReturn (*)(unsigned int*)>(GetProcAddress(api.module, "nvmlDeviceGetCount_v2"));
            api.getHandle = reinterpret_cast<NvmlReturn (*)(unsigned int, NvmlDevice*)>(GetProcAddress(api.module, "nvmlDeviceGetHandleByIndex_v2"));
            if (api.init == nullptr)
               api.init = reinterpret_cast<NvmlReturn (*)()>(GetProcAddress(api.module, "nvmlInit"));
            if (api.getCount == nullptr)
               api.getCount = reinterpret_cast<NvmlReturn (*)(unsigned int*)>(GetProcAddress(api.module, "nvmlDeviceGetCount"));
            if (api.getHandle == nullptr)
               api.getHandle = reinterpret_cast<NvmlReturn (*)(unsigned int, NvmlDevice*)>(GetProcAddress(api.module, "nvmlDeviceGetHandleByIndex"));
            api.getUtilization = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlUtilization*)>(GetProcAddress(api.module, "nvmlDeviceGetUtilizationRates"));
            api.getMemory = reinterpret_cast<NvmlReturn (*)(NvmlDevice, NvmlMemory*)>(GetProcAddress(api.module, "nvmlDeviceGetMemoryInfo"));
            api.getName = reinterpret_cast<NvmlReturn (*)(NvmlDevice, char*, unsigned int)>(GetProcAddress(api.module, "nvmlDeviceGetName"));
            if (api.init == nullptr || api.getCount == nullptr || api.getHandle == nullptr ||
                api.getUtilization == nullptr || api.getMemory == nullptr || api.init() != 0)
               api.module = nullptr;
         }
      }

      if (api.module == nullptr)
      {
         out = GpuStats{};
         return false;
      }

      const auto now = std::chrono::steady_clock::now();
      if (api.cached.available && now - api.lastPoll < std::chrono::milliseconds(500))
      {
         out = api.cached;
         return true;
      }

      unsigned int count = 0;
      if (api.getCount(&count) != 0 || count == 0)
      {
         out = GpuStats{};
         return false;
      }
      if (api.selected < 0 || api.selected >= (int)count)
      {
         unsigned long long largest = 0;
         for (unsigned int i = 0; i < count; ++i)
         {
            NvmlDevice device = nullptr;
            NvmlMemory memory {};
            if (api.getHandle(i, &device) == 0 && api.getMemory(device, &memory) == 0 && memory.total > largest)
            {
               largest = memory.total;
               api.selected = (int)i;
            }
         }
      }

      NvmlDevice device = nullptr;
      NvmlUtilization utilization {};
      NvmlMemory memory {};
      if (api.selected < 0 || api.getHandle((unsigned int)api.selected, &device) != 0 ||
          api.getUtilization(device, &utilization) != 0 || api.getMemory(device, &memory) != 0)
      {
         out = GpuStats{};
         return false;
      }

      GpuStats current;
      current.available = true;
      current.gpuPercent = (float)utilization.gpu;
      current.memoryUsedBytes = memory.used;
      current.memoryTotalBytes = memory.total;
      current.memoryPercent = memory.total > 0 ? (float)(100.0 * (double)memory.used / (double)memory.total) : 0.0f;
      if (api.getName != nullptr)
      {
         char name[96] = {};
         if (api.getName(device, name, (unsigned int)sizeof(name)) == 0)
            current.name = name;
      }
      api.cached = current;
      api.lastPoll = now;
      out = current;
      return true;
   }

   namespace
   {
      std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gJuce;

      std::wstring Utf8ToWide(const std::string& text)
      {
         if (text.empty()) return {};
         const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
         std::wstring out((size_t)std::max(0, count), L'\0');
         if (count > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), count);
         if (!out.empty()) out.resize(out.size() - 1);
         return out;
      }

      std::string WideToUtf8(const std::wstring& text)
      {
         if (text.empty()) return {};
         const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
         std::string out((size_t)count, '\0');
         WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), count, nullptr, nullptr);
         return out;
      }

      std::string KnownFolder(REFKNOWNFOLDERID id)
      {
         PWSTR raw = nullptr;
         if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)) || raw == nullptr) return {};
         std::wstring value(raw);
         CoTaskMemFree(raw);
         return WideToUtf8(value);
      }

      std::string SettingsDir()
      {
         std::string base = KnownFolder(FOLDERID_LocalAppData);
         if (base.empty()) base = fs::temp_directory_path().u8string();
         fs::path dir = fs::u8path(base) / "Infinite";
         std::error_code ec;
         fs::create_directories(dir, ec);
         return dir.u8string();
      }

      std::wstring FfmpegExecutable()
      {
         std::vector<wchar_t> modulePath(32768, L'\0');
         const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), (DWORD)modulePath.size());
         if (length > 0 && length < modulePath.size())
         {
            const fs::path besideExe = fs::path(std::wstring(modulePath.data(), length)).parent_path() / L"ffmpeg.exe";
            std::error_code ec;
            if (fs::exists(besideExe, ec))
               return besideExe.wstring();
         }
         return L"ffmpeg.exe";
      }

      // Owner of every file dialog (the editor window), see SetFileDialogOwner.
      std::atomic<HWND> gFileDialogOwner { nullptr };

      // Turbo: may run on a worker thread (main.cpp runs dialogs off the
      // render thread so playback and output keep going); it initialises its
      // own STA apartment and never touches JUCE.
      std::string RunFileDialog(bool save, bool folder, const wchar_t* title,
                                const std::vector<std::pair<std::wstring, std::wstring>>& filters,
                                const std::string& suggested = {}, const std::string& initialDir = {})
      {
         const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
         IFileDialog* dialog = nullptr;
         HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
         if (FAILED(hr) || dialog == nullptr)
         {
            if (SUCCEEDED(init)) CoUninitialize();
            return {};
         }

         dialog->SetTitle(title);
         DWORD options = 0;
         dialog->GetOptions(&options);
         options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
         if (folder) options |= FOS_PICKFOLDERS;
         dialog->SetOptions(options);

         std::vector<COMDLG_FILTERSPEC> specs;
         for (const auto& f : filters) specs.push_back({ f.first.c_str(), f.second.c_str() });
         if (!specs.empty()) dialog->SetFileTypes((UINT)specs.size(), specs.data());
         // Save dialogs append the first filter's extension ("*.inf" -> inf)
         // when the user types a bare name.
         if (save && !filters.empty())
         {
            std::wstring ext = filters.front().second;
            const size_t semi = ext.find(L';');
            if (semi != std::wstring::npos) ext = ext.substr(0, semi);
            if (ext.rfind(L"*.", 0) == 0) ext = ext.substr(2);
            if (!ext.empty() && ext != L"*") dialog->SetDefaultExtension(ext.c_str());
         }
         if (!suggested.empty()) dialog->SetFileName(Utf8ToWide(suggested).c_str());
         if (!initialDir.empty())
         {
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(Utf8ToWide(initialDir).c_str(), nullptr, IID_PPV_ARGS(&item))))
            {
               dialog->SetFolder(item);
               item->Release();
            }
         }

         std::string result;
         if (SUCCEEDED(dialog->Show(gFileDialogOwner.load())))
         {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item)
            {
               PWSTR path = nullptr;
               if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
               {
                  result = WideToUtf8(path);
                  CoTaskMemFree(path);
               }
               item->Release();
            }
         }
         dialog->Release();
         if (SUCCEEDED(init)) CoUninitialize();
         return result;
      }

      uint32_t HashId(const std::string& text)
      {
         uint32_t h = 2166136261u;
         for (unsigned char c : text) { h ^= c; h *= 16777619u; }
         return h == 0 ? 1 : h;
      }

      void FlipRgbaRows(std::vector<unsigned char>& pixels, int w, int h)
      {
         const size_t stride = (size_t)w * 4;
         std::vector<unsigned char> row(stride);
         for (int y = 0; y < h / 2; ++y)
         {
            unsigned char* a = pixels.data() + (size_t)y * stride;
            unsigned char* b = pixels.data() + (size_t)(h - 1 - y) * stride;
            std::memcpy(row.data(), a, stride);
            std::memcpy(a, b, stride);
            std::memcpy(b, row.data(), stride);
         }
      }

      bool LoadImageWithWic(const std::string& path, std::vector<unsigned char>& pixels,
                            int& width, int& height)
      {
         const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
         IWICImagingFactory* factory = nullptr;
         IWICBitmapDecoder* decoder = nullptr;
         IWICBitmapFrameDecode* frame = nullptr;
         IWICFormatConverter* converter = nullptr;
         HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
         if (SUCCEEDED(hr))
            hr = factory->CreateDecoderFromFilename(Utf8ToWide(path).c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
         if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
         if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
         if (SUCCEEDED(hr))
            hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
         UINT w = 0, h = 0;
         if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
         if (SUCCEEDED(hr) && w > 0 && h > 0)
         {
            const UINT stride = w * 4;
            pixels.resize((size_t)stride * h);
            hr = converter->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data());
         }
         if (converter) converter->Release();
         if (frame) frame->Release();
         if (decoder) decoder->Release();
         if (factory) factory->Release();
         if (SUCCEEDED(com)) CoUninitialize();
         if (FAILED(hr)) return false;
         width = (int)w; height = (int)h;
         FlipRgbaRows(pixels, width, height);
         return true;
      }

      bool RunHiddenProcess(const std::wstring& command)
      {
         STARTUPINFOW startup { sizeof(startup) };
         PROCESS_INFORMATION process {};
         std::vector<wchar_t> mutableCommand(command.begin(), command.end());
         mutableCommand.push_back(L'\0');
         if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
         WaitForSingleObject(process.hProcess, INFINITE);
         DWORD code = 1;
         GetExitCodeProcess(process.hProcess, &code);
         CloseHandle(process.hThread);
         CloseHandle(process.hProcess);
         return code == 0;
      }
   }

   void EnsureJuceInitialised()
   {
      if (!gJuce) gJuce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
   }

   void PreciseSleep(double seconds)
   {
      if (seconds <= 0.0)
         return;
      thread_local HANDLE timer = CreateWaitableTimerExW(
         nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
      if (timer != nullptr)
      {
         LARGE_INTEGER due;
         due.QuadPart = -(LONGLONG)(seconds * 1.0e7); // relative, 100 ns units
         if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0))
         {
            WaitForSingleObject(timer, INFINITE);
            return;
         }
      }
      static const bool periodSet = (timeBeginPeriod(1) == 0 /* TIMERR_NOERROR */);
      (void)periodSet;
      Sleep((DWORD)std::max(0.0, seconds * 1000.0));
   }

   void TerminateNow(int code)
   {
      TerminateProcess(GetCurrentProcess(), (UINT)code);
   }

   void BoostRenderThread()
   {
      DWORD taskIndex = 0;
      HANDLE task = AvSetMmThreadCharacteristicsW(L"Games", &taskIndex);
      if (task != nullptr)
      {
         AvSetMmThreadPriority(task, AVRT_PRIORITY_HIGH);
         RuntimeLog::Write("render thread registered with MMCSS (Games)");
      }
      else
         RuntimeLog::Write("MMCSS registration refused (error %lu)", GetLastError());
   }

   void PreventAppNap() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED); }

   void ConfigureOutputWindow(GLFWwindow* window, bool borderless, bool topmost,
                              bool hideCursor)
   {
      if (window == nullptr)
         return;
      HWND hwnd = glfwGetWin32Window(window);
      if (hwnd == nullptr)
         return;

      LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (borderless)
         style = (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP;
      else
         style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_OVERLAPPEDWINDOW;
      SetWindowLongPtrW(hwnd, GWL_STYLE, style);

      SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                   0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      glfwSetInputMode(window, GLFW_CURSOR, hideCursor ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
   }

   void SetFileDialogOwner(GLFWwindow* window)
   {
      EnsureJuceInitialised();
      gFileDialogOwner.store(window != nullptr ? glfwGetWin32Window(window) : nullptr);
   }

   void ReassertOutputWindowTopmost(GLFWwindow* window)
   {
      if (window == nullptr)
         return;
      HWND hwnd = glfwGetWin32Window(window);
      // Only when something actually took the topmost flag away: re-asserting
      // it every half second on a window that already has it made Windows
      // re-evaluate the z-order and the focused fullscreen editor flicker.
      if (hwnd != nullptr && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)
         SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
   }

   std::string OpenImageDialog()
   {
      return RunFileDialog(false, false, L"Open image", {
         {L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.heic;*.heif;*.exr;*.hdr"}, {L"All files", L"*.*"} });
   }
   std::string OpenHdrDialog() { return RunFileDialog(false, false, L"Open HDR image", {{L"HDR images", L"*.hdr;*.exr"}}); }
   std::string OpenModelDialog() { return RunFileDialog(false, false, L"Open 3D model", {{L"3D models", L"*.obj;*.ply;*.stl;*.fbx;*.gltf;*.glb;*.dae;*.3ds"}}); }
   std::string OpenPatchDialog() { return RunFileDialog(false, false, L"Open Infinite patch", {{L"Infinite patches", L"*.inf"}}); }
   std::string SavePatchDialog(const std::string& suggested) { return RunFileDialog(true, false, L"Save Infinite patch", {{L"Infinite patches", L"*.inf"}}, suggested); }
   std::string SaveVideoDialog(const std::string& suggested, const std::string& initialDir)
   {
      return RunFileDialog(true, false, L"Save output video",
                           {{L"MP4 video", L"*.mp4"}, {L"QuickTime movie", L"*.mov"}},
                           suggested, initialDir);
   }
   std::string OpenVideoDialog() { return RunFileDialog(false, false, L"Open video", {{L"Video", L"*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v"}}); }
   std::string OpenAudioDialog() { return RunFileDialog(false, false, L"Open audio", {{L"Audio", L"*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.opus"}}); }
   std::string OpenFolderDialog(const char* title, const std::string& initialDir)
   { return RunFileDialog(false, true, Utf8ToWide(title ? title : "Select folder").c_str(), {}, {}, initialDir); }

   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& out, int& w, int& h, std::string& error)
   {
      cv::Mat src = cv::imread(path, cv::IMREAD_UNCHANGED);
      if (src.empty())
      {
         if (LoadImageWithWic(path, out, w, h)) return true;
         error = "could not decode image with OpenCV or Windows Imaging Component";
         return false;
      }
      cv::Mat rgba;
      if (src.channels() == 4) cv::cvtColor(src, rgba, cv::COLOR_BGRA2RGBA);
      else if (src.channels() == 3) cv::cvtColor(src, rgba, cv::COLOR_BGR2RGBA);
      else if (src.channels() == 1) cv::cvtColor(src, rgba, cv::COLOR_GRAY2RGBA);
      else { error = "unsupported image channel layout"; return false; }
      cv::flip(rgba, rgba, 0);
      w = rgba.cols; h = rgba.rows;
      out.assign(rgba.datastart, rgba.dataend);
      return true;
   }

   bool LoadImageFloatRGB(const std::string& path, std::vector<float>& out, int& w, int& h, std::string& error)
   {
      cv::Mat src = cv::imread(path, cv::IMREAD_ANYDEPTH | cv::IMREAD_COLOR);
      if (src.empty()) { error = "could not decode HDR/EXR image"; return false; }
      cv::Mat rgb, f32;
      cv::cvtColor(src, rgb, cv::COLOR_BGR2RGB);
      const double scale = src.depth() == CV_8U ? 1.0 / 255.0 : (src.depth() == CV_16U ? 1.0 / 65535.0 : 1.0);
      rgb.convertTo(f32, CV_32F, scale);
      cv::flip(f32, f32, 0);
      w = f32.cols; h = f32.rows;
      out.assign((float*)f32.datastart, (float*)f32.dataend);
      return true;
   }

   bool LoadModel(const std::string& path, std::vector<ModelVertex>& vertices,
                  std::vector<unsigned int>& indices, std::string& error)
   {
      Assimp::Importer importer;
      const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
         aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_PreTransformVertices |
         aiProcess_FlipUVs);
      if (!scene || !scene->HasMeshes()) { error = importer.GetErrorString(); return false; }
      vertices.clear(); indices.clear();
      for (unsigned m = 0; m < scene->mNumMeshes; ++m)
      {
         const aiMesh* mesh = scene->mMeshes[m];
         const unsigned base = (unsigned)vertices.size();
         for (unsigned i = 0; i < mesh->mNumVertices; ++i)
         {
            ModelVertex v;
            v.px = mesh->mVertices[i].x; v.py = mesh->mVertices[i].y; v.pz = mesh->mVertices[i].z;
            if (mesh->HasNormals()) { v.nx = mesh->mNormals[i].x; v.ny = mesh->mNormals[i].y; v.nz = mesh->mNormals[i].z; }
            if (mesh->HasTextureCoords(0)) { v.u = mesh->mTextureCoords[0][i].x; v.v = mesh->mTextureCoords[0][i].y; }
            vertices.push_back(v);
         }
         for (unsigned f = 0; f < mesh->mNumFaces; ++f)
            for (unsigned j = 0; j < mesh->mFaces[f].mNumIndices; ++j)
               indices.push_back(base + mesh->mFaces[f].mIndices[j]);
      }
      return !indices.empty();
   }

   const std::vector<std::string>& AvailableFontFamilies()
   {
      EnsureJuceInitialised();
      static std::vector<std::string> fonts;
      if (fonts.empty())
      {
         for (const auto& name : juce::Font::findAllTypefaceNames()) fonts.push_back(name.toStdString());
         std::sort(fonts.begin(), fonts.end());
         if (fonts.empty()) fonts.push_back("Arial");
      }
      return fonts;
   }

   bool GetTextOutlines(const std::string& text, const std::string& fontName, float tracking,
                        std::vector<TextContour>& contours, std::string& error)
   {
      EnsureJuceInitialised();
      contours.clear();
      juce::Font font(juce::FontOptions(fontName.empty() ? "Arial" : fontName, 1000.0f, juce::Font::plain));
      font.setExtraKerningFactor(tracking / 1000.0f);
      juce::GlyphArrangement glyphs;
      glyphs.addLineOfText(font, juce::String::fromUTF8(text.c_str()), 0.0f, 0.0f);
      juce::Path path;
      glyphs.createPath(path);
      if (path.isEmpty()) { error = "font produced no glyph outlines"; return false; }
      juce::PathFlatteningIterator it(path, juce::AffineTransform(), 1.0f);
      TextContour current;
      while (it.next())
      {
         if (it.subPathIndex == 0 && !current.points.empty()) { contours.push_back(std::move(current)); current = {}; }
         if (current.points.empty()) { current.points.push_back(it.x1 / 700.0f); current.points.push_back(-it.y1 / 700.0f); }
         current.points.push_back(it.x2 / 700.0f); current.points.push_back(-it.y2 / 700.0f);
      }
      if (!current.points.empty()) contours.push_back(std::move(current));
      return !contours.empty();
   }

   // ---- video file decoding (Turbo) -------------------------------------
   // One worker thread per clip owns the cv::VideoCapture. It decodes the
   // frame the render thread asked for and then reads ahead in the playback
   // direction into a small cache, so steady playback finds its next frame
   // already decoded instead of waiting a frame for it.
   //  - forward: sequential read-ahead; small jumps (speed > 1, a slow UI
   //    frame) are reached with grab() instead of a keyframe seek.
   //  - reverse: one seek per batch. The worker decodes the frames just
   //    below the target in one forward run and serves them backwards,
   //    instead of paying a keyframe seek for every single frame.
   // Frames are BGR8 in GL row order (bottom-up): the GPU swizzles BGR on
   // upload, so the CPU no longer converts every pixel to RGBA.
   struct VideoHandle
   {
      struct CachedFrame
      {
         long long index = -1;
         std::vector<unsigned char> pixels;
      };

      cv::VideoCapture capture;
      std::atomic<int> width { 0 };
      std::atomic<int> height { 0 };
      double duration = 0.0;
      double fps = 30.0;
      long long frameCount = 0;          // guarded by frameMutex (shrinks if the container lied)

      std::mutex frameMutex;
      std::condition_variable frameReady;
      std::thread decoder;
      bool stopping = false;
      bool requestPending = false;
      long long requestedFrame = 0;
      long long lastQueuedFrame = -1;
      int direction = 1;
      uint64_t requestSerial = 0;
      uint64_t publishedSerial = 0;
      uint64_t consumedSerial = 0;
      std::vector<unsigned char> publishedPixels;
      std::deque<CachedFrame> cache;
      std::vector<std::vector<unsigned char>> freeBuffers;
      size_t cacheCapacity = 4;

      long long lastDecodedFrame = -1000000; // decoder thread only

      ~VideoHandle()
      {
         {
            std::lock_guard<std::mutex> lock(frameMutex);
            stopping = true;
         }
         frameReady.notify_all();
         if (decoder.joinable())
            decoder.join();
      }
   };

   namespace
   {
      constexpr long long kVideoGrabInsteadOfSeek = 12;
      constexpr size_t kVideoCacheBudgetBytes = 160ull * 1024ull * 1024ull;

      // Decoder thread only. Positions the capture on `target` as cheaply as
      // possible and writes the frame into `out` as bottom-up BGR8.
      bool VideoDecodeInto(VideoHandle* v, long long target, std::vector<unsigned char>& out)
      {
         const long long gap = target - (v->lastDecodedFrame + 1);
         bool positioned = false;
         if (gap >= 0 && gap <= kVideoGrabInsteadOfSeek)
         {
            positioned = true;
            for (long long i = 0; i < gap; ++i)
            {
               if (!v->capture.grab())
               {
                  positioned = false;
                  break;
               }
            }
         }
         if (!positioned)
            v->capture.set(cv::CAP_PROP_POS_FRAMES, (double)target);

         cv::Mat frame;
         if (!v->capture.read(frame) || frame.empty())
         {
            v->lastDecodedFrame = -1000000; // force a real seek next time
            return false;
         }
         v->lastDecodedFrame = target;

         if (frame.type() != CV_8UC3)
         {
            cv::Mat converted;
            if (frame.channels() == 4)
               cv::cvtColor(frame, converted, cv::COLOR_BGRA2BGR);
            else if (frame.channels() == 1)
               cv::cvtColor(frame, converted, cv::COLOR_GRAY2BGR);
            else
               frame.convertTo(converted, CV_8UC3);
            frame = converted;
         }

         out.resize((size_t)frame.cols * (size_t)frame.rows * 3);
         cv::Mat dst(frame.rows, frame.cols, CV_8UC3, out.data());
         cv::flip(frame, dst, 0);
         v->width.store(frame.cols, std::memory_order_relaxed);
         v->height.store(frame.rows, std::memory_order_relaxed);
         return true;
      }

      // Called with frameMutex held. Next frame worth decoding speculatively,
      // or -1 when the cache is full / playing backwards / at the end.
      long long VideoPrefetchTarget(const VideoHandle* v)
      {
         if (v->direction <= 0 || v->cache.size() >= v->cacheCapacity)
            return -1;
         long long base = v->lastQueuedFrame;
         if (!v->cache.empty())
            base = std::max(base, v->cache.back().index);
         const long long next = base + 1;
         if (next < 0 || (v->frameCount > 0 && next >= v->frameCount))
            return -1;
         return next;
      }

      // Called with frameMutex held.
      void VideoRecycle(VideoHandle* v, std::vector<unsigned char>&& buffer)
      {
         if (v->freeBuffers.size() < 4 && buffer.capacity() > 0)
            v->freeBuffers.push_back(std::move(buffer));
      }

      // Called with frameMutex held.
      std::vector<unsigned char> VideoTakeBuffer(VideoHandle* v)
      {
         if (v->freeBuffers.empty())
            return {};
         std::vector<unsigned char> buffer = std::move(v->freeBuffers.back());
         v->freeBuffers.pop_back();
         return buffer;
      }

      void VideoDecodeWorker(VideoHandle* v)
      {
         for (;;)
         {
            long long target = -1;
            uint64_t serial = 0;
            bool prefetch = false;
            int direction = 1;
            std::vector<unsigned char> scratch;
            {
               std::unique_lock<std::mutex> lock(v->frameMutex);
               v->frameReady.wait(lock, [v]()
               {
                  return v->stopping || v->requestPending || VideoPrefetchTarget(v) >= 0;
               });
               if (v->stopping)
                  return;

               direction = v->direction;
               if (v->requestPending)
               {
                  target = v->requestedFrame;
                  serial = v->requestSerial;
                  v->requestPending = false;

                  // Serve from the cache when read-ahead already has it and
                  // drop entries the playhead has moved past.
                  bool served = false;
                  for (auto it = v->cache.begin(); it != v->cache.end();)
                  {
                     if (it->index == target && !served)
                     {
                        VideoRecycle(v, std::move(v->publishedPixels));
                        v->publishedPixels = std::move(it->pixels);
                        v->publishedSerial = serial;
                        served = true;
                        it = v->cache.erase(it);
                     }
                     else if ((direction > 0 && it->index < target) ||
                              (direction < 0 && it->index > target))
                     {
                        VideoRecycle(v, std::move(it->pixels));
                        it = v->cache.erase(it);
                     }
                     else
                        ++it;
                  }
                  if (served)
                     continue;
               }
               else
               {
                  target = VideoPrefetchTarget(v);
                  prefetch = true;
               }
               scratch = VideoTakeBuffer(v);
            }

            // Reverse playback: decode [target - N + 1, target] in one forward
            // run, keep the lower frames for the next requests.
            if (!prefetch && direction < 0)
            {
               const long long batch = (long long)std::max<size_t>(1, v->cacheCapacity);
               const long long start = std::max(0LL, target - batch + 1);
               std::vector<VideoHandle::CachedFrame> decoded;
               for (long long f = start; f <= target; ++f)
               {
                  VideoHandle::CachedFrame cf;
                  cf.index = f;
                  if (f == target)
                     cf.pixels = std::move(scratch);
                  else
                  {
                     std::lock_guard<std::mutex> lock(v->frameMutex);
                     cf.pixels = VideoTakeBuffer(v);
                  }
                  if (VideoDecodeInto(v, f, cf.pixels))
                     decoded.push_back(std::move(cf));
               }
               std::lock_guard<std::mutex> lock(v->frameMutex);
               for (auto& cf : decoded)
               {
                  if (cf.index == target)
                  {
                     VideoRecycle(v, std::move(v->publishedPixels));
                     v->publishedPixels = std::move(cf.pixels);
                     v->publishedSerial = serial;
                  }
                  else if (v->cache.size() < v->cacheCapacity)
                     v->cache.push_back(std::move(cf));
                  else
                     VideoRecycle(v, std::move(cf.pixels));
               }
               continue;
            }

            const bool ok = VideoDecodeInto(v, target, scratch);
            std::lock_guard<std::mutex> lock(v->frameMutex);
            if (!ok)
            {
               // A failed read-ahead means the container over-reported its
               // length: stop speculating past this point.
               if (prefetch)
                  v->frameCount = target;
               VideoRecycle(v, std::move(scratch));
               continue;
            }
            if (prefetch)
            {
               VideoHandle::CachedFrame cf;
               cf.index = target;
               cf.pixels = std::move(scratch);
               v->cache.push_back(std::move(cf));
            }
            else
            {
               VideoRecycle(v, std::move(v->publishedPixels));
               v->publishedPixels = std::move(scratch);
               v->publishedSerial = serial;
            }
         }
      }
   }

   VideoHandle* VideoOpen(const std::string& path, std::string& error)
   {
      auto handle = std::make_unique<VideoHandle>();
      // Hardware decode (D3D11VA/DXVA through FFmpeg) unless disabled with
      // INFINITE_VIDEO_HWACCEL=0. Falls back to software, then to any backend.
      bool opened = false;
      const char* hw = std::getenv("INFINITE_VIDEO_HWACCEL");
      if (hw == nullptr || std::strcmp(hw, "0") != 0)
      {
         const std::vector<int> params = { cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_ANY };
         opened = handle->capture.open(path, cv::CAP_FFMPEG, params);
      }
      if (!opened)
         opened = handle->capture.open(path, cv::CAP_FFMPEG);
      if (!opened)
         opened = handle->capture.open(path);
      if (!opened)
      {
         error = "OpenCV could not open video";
         return nullptr;
      }
      const int w = (int)handle->capture.get(cv::CAP_PROP_FRAME_WIDTH);
      const int h = (int)handle->capture.get(cv::CAP_PROP_FRAME_HEIGHT);
      handle->width.store(w, std::memory_order_relaxed);
      handle->height.store(h, std::memory_order_relaxed);
      handle->fps = handle->capture.get(cv::CAP_PROP_FPS);
      if (handle->fps <= 0.0)
         handle->fps = 30.0;
      const double frames = handle->capture.get(cv::CAP_PROP_FRAME_COUNT);
      handle->frameCount = frames > 0 ? (long long)frames : 0;
      handle->duration = frames > 0 ? frames / handle->fps : 0.0;
      const size_t frameBytes = (size_t)std::max(1, w) * (size_t)std::max(1, h) * 3;
      handle->cacheCapacity = std::clamp<size_t>(kVideoCacheBudgetBytes / frameBytes, 2, 8);
      RuntimeLog::Write("video opened: %dx%d %.2f fps, hwaccel=%d, read-ahead=%d frames",
                        w, h, handle->fps,
                        (int)handle->capture.get(cv::CAP_PROP_HW_ACCELERATION),
                        (int)handle->cacheCapacity);
      VideoHandle* raw = handle.get();
      handle->decoder = std::thread(VideoDecodeWorker, raw);
      return handle.release();
   }
   void VideoClose(VideoHandle* h) { delete h; }
   int VideoWidth(VideoHandle* h) { return h ? h->width.load(std::memory_order_relaxed) : 0; }
   int VideoHeight(VideoHandle* h) { return h ? h->height.load(std::memory_order_relaxed) : 0; }
   double VideoDuration(VideoHandle* h) { return h ? h->duration : 0.0; }
   bool VideoFrameAt(VideoHandle* h, double seconds, std::vector<unsigned char>& out)
   {
      if (!h) return false;
      const long long targetFrame = std::max(0LL, (long long)std::floor(std::max(0.0, seconds) * h->fps));
      bool produced = false;
      {
         std::lock_guard<std::mutex> lock(h->frameMutex);
         if (targetFrame != h->lastQueuedFrame)
         {
            if (h->lastQueuedFrame >= 0)
               h->direction = targetFrame < h->lastQueuedFrame ? -1 : 1;
            h->requestedFrame = targetFrame;
            h->lastQueuedFrame = targetFrame;
            ++h->requestSerial;
            h->requestPending = true;
         }
         if (h->publishedSerial != h->consumedSerial && !h->publishedPixels.empty())
         {
            // Swap instead of copy: the caller's previous buffer goes back to
            // the decoder as the next scratch buffer.
            out.swap(h->publishedPixels);
            h->consumedSerial = h->publishedSerial;
            produced = true;
         }
      }
      h->frameReady.notify_one();
      return produced;
   }

   namespace
   {
      fs::path MattingModelPath(MattingMode mode)
      {
         const char* filename = mode == MattingMode::Person ? "u2net_human_seg.onnx" : "u2net.onnx";
         fs::path model = fs::u8path(SettingsDir()) / "models" / filename;
         if (!fs::exists(model))
            model = fs::u8path(ExecutablePath()).parent_path() / "models" / filename;
         return model;
      }

      cv::Mat PrepareMattingInput(const std::vector<unsigned char>& rgba, int width, int height)
      {
         cv::Mat src(height, width, CV_8UC4, const_cast<unsigned char*>(rgba.data()));
         cv::Mat top, rgb;
         cv::flip(src, top, 0);
         cv::cvtColor(top, rgb, cv::COLOR_RGBA2RGB);
         cv::Mat blob = cv::dnn::blobFromImage(rgb, 1.0 / 255.0, {320, 320},
                                               {0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0},
                                               false, false);
         const float stdv[3] = {0.229f, 0.224f, 0.225f};
         const int plane = blob.size[2] * blob.size[3];
         for (int c = 0; c < 3; ++c)
         {
            float* values = blob.ptr<float>(0, c);
            for (int i = 0; i < plane; ++i) values[i] /= stdv[c];
         }
         return blob;
      }

      bool FinishMattingMask(const float* prediction, size_t count, int predictionWidth,
                             int predictionHeight, int width, int height,
                             std::vector<unsigned char>& mask, std::string& error)
      {
         if (prediction == nullptr || predictionWidth <= 0 || predictionHeight <= 0 ||
             count < (size_t)predictionWidth * predictionHeight)
         {
            error = "the segmentation model returned an invalid tensor";
            return false;
         }
         cv::Mat view(predictionHeight, predictionWidth, CV_32F,
                      const_cast<float*>(prediction));
         cv::Mat m = view.clone();
         double lo = 0.0, hi = 1.0;
         cv::minMaxLoc(m, &lo, &hi);
         m = (m - lo) / std::max(1.0e-6, hi - lo);
         cv::resize(m, m, {width, height}, 0, 0, cv::INTER_LANCZOS4);
         m.convertTo(m, CV_8U, 255.0);
         cv::GaussianBlur(m, m, {5,5}, 0.0);
         cv::flip(m, m, 0);
         mask.assign(m.datastart, m.dataend);
         return true;
      }

      bool RunCpuMatting(const fs::path& model, MattingMode mode, const cv::Mat& blob,
                         int width, int height, std::vector<unsigned char>& mask,
                         std::string& error)
      {
         static cv::dnn::Net subjectNet;
         static cv::dnn::Net personNet;
         cv::dnn::Net& net = mode == MattingMode::Person ? personNet : subjectNet;
         if (net.empty())
         {
            net = cv::dnn::readNetFromONNX(model.u8string());
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
         }
         net.setInput(blob);
         cv::Mat prediction = net.forward();
         int predictionHeight = 320;
         int predictionWidth = 320;
         if (prediction.dims >= 2)
         {
            predictionHeight = prediction.size[prediction.dims - 2];
            predictionWidth = prediction.size[prediction.dims - 1];
         }
         return FinishMattingMask(prediction.ptr<float>(), prediction.total(), predictionWidth,
                                  predictionHeight, width, height, mask, error);
      }

#if INFINITE_HAS_WINDOWS_ML
      struct DirectMlAdapter
      {
         int deviceId = 0;
         std::string name = "default GPU";
      };

      DirectMlAdapter PreferredDirectMlAdapter()
      {
         DirectMlAdapter selected;
         IDXGIFactory1* factory = nullptr;
         if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || factory == nullptr)
            return selected;

         SIZE_T largestDedicatedMemory = 0;
         for (UINT index = 0; ; ++index)
         {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
               break;
            if (adapter == nullptr)
               continue;
            DXGI_ADAPTER_DESC1 desc {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
                (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                desc.DedicatedVideoMemory >= largestDedicatedMemory)
            {
               largestDedicatedMemory = desc.DedicatedVideoMemory;
               selected.deviceId = (int)index;
               selected.name = WideToUtf8(desc.Description);
            }
            adapter->Release();
         }
         factory->Release();
         return selected;
      }

      struct DirectMlMattingSession
      {
         std::unique_ptr<Ort::Session> session;
         std::string modelPath;
         std::string inputName;
         std::string outputName;
         DirectMlAdapter adapter;
         bool disabled = false;
         std::string disabledError;

         bool Ensure(const fs::path& model, std::string& error)
         {
            if (session && modelPath == model.u8string())
               return true;
            if (disabled)
            {
               error = disabledError;
               return false;
            }
            try
            {
               static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Infinite-DirectML");
               Ort::SessionOptions options;
               options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
               options.DisableMemPattern();
               options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
               adapter = PreferredDirectMlAdapter();
               Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, adapter.deviceId));

               auto candidate = std::make_unique<Ort::Session>(env, model.c_str(), options);
               Ort::AllocatorWithDefaultOptions allocator;
               auto input = candidate->GetInputNameAllocated(0, allocator);
               auto output = candidate->GetOutputNameAllocated(0, allocator);
               inputName = input.get();
               outputName = output.get();
               modelPath = model.u8string();
               session = std::move(candidate);
               return true;
            }
            catch (const Ort::Exception& e)
            {
               session.reset();
               disabled = true;
               disabledError = std::string("DirectML session failed: ") + e.what();
               error = disabledError;
               return false;
            }
         }

         bool Run(const fs::path& model, const cv::Mat& blob, int width, int height,
                  std::vector<unsigned char>& mask, std::string& error)
         {
            if (!Ensure(model, error))
               return false;
            try
            {
               std::array<int64_t, 4> shape {1, 3, 320, 320};
               Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(
                  OrtArenaAllocator, OrtMemTypeDefault);
               Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                  memory, const_cast<float*>(blob.ptr<float>()), blob.total(),
                  shape.data(), shape.size());
               const char* inputNames[] = { inputName.c_str() };
               const char* outputNames[] = { outputName.c_str() };
               auto outputs = session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1,
                                           outputNames, 1);
               if (outputs.empty() || !outputs[0].IsTensor())
               {
                  error = "DirectML returned no segmentation tensor";
                  return false;
               }
               const auto info = outputs[0].GetTensorTypeAndShapeInfo();
               const std::vector<int64_t> outShape = info.GetShape();
               int predictionHeight = 320;
               int predictionWidth = 320;
               if (outShape.size() >= 2)
               {
                  predictionHeight = (int)outShape[outShape.size() - 2];
                  predictionWidth = (int)outShape[outShape.size() - 1];
               }
               return FinishMattingMask(outputs[0].GetTensorData<float>(), info.GetElementCount(),
                                        predictionWidth, predictionHeight, width, height,
                                        mask, error);
            }
            catch (const Ort::Exception& e)
            {
               session.reset();
               disabled = true;
               disabledError = std::string("DirectML inference failed: ") + e.what();
               error = disabledError;
               return false;
            }
         }
      };
#endif
   }

   bool SubjectMask(const std::vector<unsigned char>& rgba, int width, int height,
                    MattingMode mode, MattingBackend backend,
                    std::vector<unsigned char>& mask, std::string& error,
                    std::string* outBackend)
   {
      if (outBackend) outBackend->clear();
      if (width <= 0 || height <= 0 || rgba.size() < (size_t)width * height * 4)
      {
         error = "bad image";
         return false;
      }

      const fs::path model = MattingModelPath(mode);
      if (!fs::exists(model))
      {
         error = model.filename().u8string() +
                 " is missing. Run install-runtime.bat or install-dependencies.bat.";
         return false;
      }

      // DirectML forbids parallel Run calls on one session. The node already
      // uses a latest-frame worker; this lock also serializes the two model
      // variants and keeps OpenCV's persistent CPU networks safe.
      static std::mutex mutex;
      std::lock_guard<std::mutex> lock(mutex);
      try
      {
         const cv::Mat blob = PrepareMattingInput(rgba, width, height);
         std::string directMlError;
#if INFINITE_HAS_WINDOWS_ML
         if (backend != MattingBackend::Cpu)
         {
            static DirectMlMattingSession subjectSession;
            static DirectMlMattingSession personSession;
            DirectMlMattingSession& session = mode == MattingMode::Person
               ? personSession : subjectSession;
            if (session.Run(model, blob, width, height, mask, directMlError))
            {
               if (outBackend)
                  *outBackend = "DirectML/DX12 - " + session.adapter.name;
               return true;
            }
            if (backend == MattingBackend::DirectML)
            {
               error = directMlError;
               return false;
            }
            RuntimeLog::Write("Remove Background DirectML fallback: %s", directMlError.c_str());
         }
#else
         if (backend == MattingBackend::DirectML)
         {
            error = "this build does not include Windows ML / DirectML";
            return false;
         }
#endif
         if (RunCpuMatting(model, mode, blob, width, height, mask, error))
         {
            if (outBackend)
               *outBackend = directMlError.empty() ? "OpenCV CPU" : "OpenCV CPU fallback";
            return true;
         }
         return false;
      }
      catch (const cv::Exception& e)
      {
         error = e.what();
         return false;
      }
      catch (const std::exception& e)
      {
         error = e.what();
         return false;
      }
   }

   namespace
   {
      constexpr size_t kInputRingFrames = 262144;
      struct InputRing
      {
         float data[2][kInputRingFrames] {};
         // Monotonic frame counters make this a latest-audio ring rather than
         // a queue of historical audio. Live monitoring must never replay the
         // several seconds accumulated while a node existed but the graph was
         // not yet consuming it.
         std::atomic<unsigned long long> readFrame {0}, writeFrame {0};
         void write(const float* const* in, int channels, int frames)
         {
            const unsigned long long start = writeFrame.load(std::memory_order_relaxed);
            for (int i = 0; i < frames; ++i)
            {
               const size_t slot = (size_t)((start + (unsigned long long)i) % kInputRingFrames);
               for (int ch = 0; ch < 2; ++ch)
                  data[ch][slot] = ch < channels && in[ch] ? in[ch][i] : 0.0f;
            }
            writeFrame.store(start + (unsigned long long)std::max(0, frames), std::memory_order_release);
         }
         int read(float* const* out, int frames, int maxChannels)
         {
            const unsigned long long written = writeFrame.load(std::memory_order_acquire);
            unsigned long long read = readFrame.load(std::memory_order_relaxed);
            if (written > read + kInputRingFrames)
               read = written - kInputRingFrames;
            // If the consumer fell behind, jump to the newest complete block.
            // At 48 kHz/128 frames this caps software-monitoring delay near one
            // block instead of the old ring's 5.46-second capacity.
            if (written > read + (unsigned long long)std::max(0, frames))
               read = written - (unsigned long long)std::max(0, frames);
            const int available = (int)std::min<unsigned long long>(
               written - read, (unsigned long long)std::max(0, frames));
            for (int i = 0; i < available; ++i)
            {
               const size_t slot = (size_t)((read + (unsigned long long)i) % kInputRingFrames);
               for (int ch = 0; ch < maxChannels; ++ch)
                  out[ch][i] = ch < 2 ? data[ch][slot] : 0.0f;
            }
            readFrame.store(read + (unsigned long long)available, std::memory_order_release);
            for (int ch = 0; ch < maxChannels; ++ch)
               std::fill(out[ch] + available, out[ch] + frames, 0.0f);
            return available > 0 ? std::min(2, maxChannels) : 0;
         }
      };

      struct AudioBridge final : juce::AudioIODeviceCallback
      {
         juce::AudioDeviceManager manager;
         AudioRenderCallback callback = nullptr;
         void* user = nullptr;
         InputRing inputRing;
         std::atomic<bool> callbackAdded {false};
         std::atomic<bool> analyzer {false};
         std::atomic<bool> spike {false};
         std::atomic<bool> configChanged {false};
         std::atomic<int> inputRefs {0};
         AudioLevels publish[2];
         std::atomic<int> ready {0};
         std::string lastDevice;
         float gain = 1.0f, attack = 0.25f, release = 0.08f;
         double sampleRate = 48000.0;
         int blockSize = 512;
         std::atomic<int> roundTripFrames {0};
         double spikePhase = 0.0;
         std::atomic<unsigned long long> spikeCallbacks {0};
         std::atomic<long long> spikeLastUs {0};
         std::atomic<long long> spikeMaxJitterUs {0};

         bool open(int inputs, int outputs, std::string& error)
         {
            EnsureJuceInitialised();
            if (manager.getCurrentAudioDevice() != nullptr) return true;
            juce::String e = manager.initialise(inputs, outputs, nullptr, true);
            if (e.isNotEmpty()) { error = e.toStdString(); return false; }
            if (!callbackAdded.exchange(true)) manager.addAudioCallback(this);
            return manager.getCurrentAudioDevice() != nullptr;
         }

         void audioDeviceAboutToStart(juce::AudioIODevice* device) override
         {
            if (!device) return;
            sampleRate = device->getCurrentSampleRate();
            blockSize = device->getCurrentBufferSizeSamples();
            roundTripFrames.store(std::max(0, device->getInputLatencyInSamples()) +
                                  std::max(0, device->getOutputLatencyInSamples()) + std::max(0, blockSize));
            lastDevice = device->getName().toStdString();
         }
         void audioDeviceStopped() override {}
         void audioDeviceError(const juce::String&) override { configChanged.store(true); }

         void audioDeviceIOCallbackWithContext(const float* const* input, int numIn,
             float* const* output, int numOut, int frames, const juce::AudioIODeviceCallbackContext&) override
         {
            for (int ch = 0; ch < numOut; ++ch) if (output[ch]) std::fill(output[ch], output[ch] + frames, 0.0f);
            if (inputRefs.load(std::memory_order_relaxed) > 0 || analyzer.load(std::memory_order_relaxed))
               inputRing.write(input, numIn, frames);

            if (analyzer.load(std::memory_order_relaxed) && numIn > 0 && input[0])
            {
               AudioLevels l;
               double sum = 0.0; float peak = 0.0f;
               for (int i = 0; i < frames; ++i) { const float v = input[0][i] * gain; sum += v*v; peak = std::max(peak, std::fabs(v)); }
               l.rms = frames > 0 ? (float)std::sqrt(sum / frames) : 0.0f; l.peak = peak;
               for (int b = 0; b < kAudioBands; ++b)
               {
                  const double freq = 30.0 * std::pow(16000.0 / 30.0, (b + 0.5) / kAudioBands);
                  const double omega = 2.0 * 3.141592653589793 * freq / sampleRate;
                  double re = 0.0, im = 0.0;
                  for (int i = 0; i < frames; ++i) { re += input[0][i] * std::cos(omega*i); im -= input[0][i] * std::sin(omega*i); }
                  l.bands[b] = frames > 0 ? (float)(2.0 * std::sqrt(re*re + im*im) / frames) : 0.0f;
               }
               l.low = std::accumulate(l.bands, l.bands + 6, 0.0f) / 6.0f;
               l.mid = std::accumulate(l.bands + 6, l.bands + 11, 0.0f) / 5.0f;
               l.high = std::accumulate(l.bands + 11, l.bands + 16, 0.0f) / 5.0f;
               const AudioLevels& previous = publish[ready.load(std::memory_order_acquire)];
               auto smooth = [&](float value, float old) { const float a = value > old ? attack : release; return old + (value - old) * std::clamp(a, 0.0f, 1.0f); };
               l.rms = smooth(l.rms, previous.rms); l.peak = smooth(l.peak, previous.peak);
               l.low = smooth(l.low, previous.low); l.mid = smooth(l.mid, previous.mid); l.high = smooth(l.high, previous.high);
               for (int b = 0; b < kAudioBands; ++b) l.bands[b] = smooth(l.bands[b], previous.bands[b]);
               l.onset = l.peak > std::max(0.08f, previous.peak * 1.6f);
               const int next = 1 - ready.load(std::memory_order_relaxed);
               publish[next] = l; ready.store(next, std::memory_order_release);
            }
            if (spike.load(std::memory_order_relaxed) && numOut > 0)
            {
               const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
               const auto previous = spikeLastUs.exchange(now);
               if (previous > 0)
               {
                  const long long expected = (long long)std::llround(1000000.0 * frames / std::max(1.0, sampleRate));
                  const long long jitter = std::llabs((now - previous) - expected);
                  long long old = spikeMaxJitterUs.load();
                  while (jitter > old && !spikeMaxJitterUs.compare_exchange_weak(old, jitter)) {}
               }
               const double step = 2.0 * 3.141592653589793 * 440.0 / std::max(1.0, sampleRate);
               for (int i = 0; i < frames; ++i)
               {
                  const float value = (float)(std::sin(spikePhase) * 0.1);
                  spikePhase += step; if (spikePhase >= 2.0 * 3.141592653589793) spikePhase -= 2.0 * 3.141592653589793;
                  for (int ch = 0; ch < numOut; ++ch) if (output[ch]) output[ch][i] += value;
               }
               spikeCallbacks.fetch_add(1, std::memory_order_relaxed);
            }
            // JUCE marks the channel-pointer array itself const, while the
            // Infinite callback only writes sample data and never rebinds a
            // channel pointer. Remove only that outer const qualification.
            if (callback) callback(const_cast<float**>(output), numOut, frames, user);
         }
      };
      // AudioDeviceManager reaches JUCE's message/device infrastructure from
      // its constructor. Keeping AudioBridge as a namespace global therefore
      // constructed it before main(), before ScopedJuceInitialiser_GUI, and
      // caused a 0xC0000005 in ntdll on Windows. A function-local static is
      // created only on first audio use, after EnsureJuceInitialised().
      AudioBridge& AudioBridgeInstance()
      {
         EnsureJuceInitialised();
         static AudioBridge instance;
         return instance;
      }
   }

   bool AudioStart(std::string& error) { if (!AudioBridgeInstance().open(2, 2, error)) return false; AudioBridgeInstance().analyzer.store(true); return true; }
   void AudioStop() { AudioBridgeInstance().analyzer.store(false); }
   bool AudioIsRunning() { return AudioBridgeInstance().analyzer.load() && AudioBridgeInstance().manager.getCurrentAudioDevice() != nullptr; }
   std::string AudioDeviceName() { return AudioBridgeInstance().lastDevice; }
   bool AudioRead(AudioLevels& out) { if (!AudioIsRunning()) return false; out = AudioBridgeInstance().publish[AudioBridgeInstance().ready.load(std::memory_order_acquire)]; return true; }
   void AudioSetSmoothing(float a, float r) { AudioBridgeInstance().attack = a; AudioBridgeInstance().release = r; }
   void AudioSetGain(float g) { AudioBridgeInstance().gain = g; }
   bool AudioSpikeStart(std::string& e)
   {
      if (!AudioBridgeInstance().open(0, 2, e)) return false;
      AudioBridgeInstance().spikePhase = 0.0; AudioBridgeInstance().spikeCallbacks.store(0); AudioBridgeInstance().spikeLastUs.store(0); AudioBridgeInstance().spikeMaxJitterUs.store(0); AudioBridgeInstance().spike.store(true);
      return true;
   }
   void AudioSpikeStop() { AudioBridgeInstance().spike.store(false); }
   AudioSpikeStats AudioSpikeGetStats() { AudioSpikeStats s; s.sampleRate = AudioBridgeInstance().sampleRate; s.blockSize = AudioBridgeInstance().blockSize; s.maxJitterMs = AudioBridgeInstance().spikeMaxJitterUs.load() / 1000.0; s.callbackCount = AudioBridgeInstance().spikeCallbacks.load(); return s; }

   bool AudioDeviceOpen(AudioRenderCallback cb, void* user, double& rate, std::string& error,
                        uint32_t requestedId, double requestedRate, int requestedFrames,
                        uint32_t requestedInputId)
   {
      if (!AudioBridgeInstance().open(2, 2, error)) return false;
      auto setup = AudioBridgeInstance().manager.getAudioDeviceSetup();
      if (requestedRate > 0) setup.sampleRate = requestedRate;
      if (requestedFrames > 0) setup.bufferSize = requestedFrames;
      if (requestedId != 0)
      {
         for (const auto& d : AudioListDevices()) if (d.isOutput && d.deviceId == requestedId) setup.outputDeviceName = d.name;
      }
      if (requestedInputId != 0)
      {
         for (const auto& d : AudioListDevices()) if (d.isInput && d.deviceId == requestedInputId) setup.inputDeviceName = d.name;
      }
      const juce::String result = AudioBridgeInstance().manager.setAudioDeviceSetup(setup, true);
      if (result.isNotEmpty()) { error = result.toStdString(); return false; }
      AudioBridgeInstance().callback = cb; AudioBridgeInstance().user = user;
      if (auto* d = AudioBridgeInstance().manager.getCurrentAudioDevice()) rate = d->getCurrentSampleRate(); else rate = 0.0;
      return rate > 0.0;
   }
   void AudioDeviceClose() { AudioBridgeInstance().callback = nullptr; AudioBridgeInstance().user = nullptr; if (!AudioBridgeInstance().analyzer.load()) AudioBridgeInstance().manager.closeAudioDevice(); }
   uint32_t AudioDeviceBufferFrames(uint32_t) { return (uint32_t)std::max(0, AudioBridgeInstance().blockSize); }
   int AudioRoundTripLatencyFrames() { return AudioBridgeInstance().roundTripFrames.load(std::memory_order_relaxed); }
   bool AudioDeviceConfigDidChange() { return AudioBridgeInstance().configChanged.exchange(false); }
   bool AudioWillSleep() { return false; }
   bool AudioDidWake() { return false; }
   void AudioDeviceDebugSimulateConfigChange() { AudioBridgeInstance().configChanged.store(true); }

   std::vector<AudioDeviceInfo> AudioListDevices()
   {
      EnsureJuceInitialised();
      std::vector<AudioDeviceInfo> out;
      auto& types = AudioBridgeInstance().manager.getAvailableDeviceTypes();
      for (auto* type : types)
      {
         if (!type) continue;
         type->scanForDevices();
         for (auto input : {false, true})
            for (const auto& name : type->getDeviceNames(input))
            {
               AudioDeviceInfo d; d.name = name.toStdString(); d.deviceId = HashId(d.name); d.isInput = input; d.isOutput = !input; out.push_back(d);
            }
      }
      return out;
   }

   bool DecodeAudioFileToBuffer(const std::string& path, SampleBuffer& out, std::string& error)
   {
      EnsureJuceInitialised();
      juce::AudioFormatManager formats; formats.registerBasicFormats();
      std::string decodePath = path;
      fs::path temporary;
      auto cleanupTemporary = [&]() { if (!temporary.empty()) { std::error_code ec; fs::remove(temporary, ec); } };
      std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(juce::String(Utf8ToWide(decodePath).c_str()))));
      if (!reader)
      {
         temporary = fs::u8path(SettingsDir()) / ("audio_decode_" + std::to_string(GetCurrentProcessId()) + "_" +
                     std::to_string(GetCurrentThreadId()) + "_" + std::to_string(GetTickCount64()) + ".wav");
         const std::wstring command = L"\"" + FfmpegExecutable() + L"\" -y -loglevel error -i \"" + Utf8ToWide(path) +
                                      L"\" -vn -c:a pcm_f32le \"" + Utf8ToWide(temporary.u8string()) + L"\"";
         if (RunHiddenProcess(command))
         {
            decodePath = temporary.u8string();
            reader.reset(formats.createReaderFor(juce::File(juce::String(Utf8ToWide(decodePath).c_str()))));
         }
      }
      if (!reader) { cleanupTemporary(); error = "unsupported or unreadable audio file (FFmpeg fallback also failed)"; return false; }
      if (reader->lengthInSamples <= 0 || reader->lengthInSamples > INT_MAX) { reader.reset(); cleanupTemporary(); error = "audio file is empty or too long"; return false; }
      out.channels = std::max(1, (int)reader->numChannels); out.numFrames = (int)reader->lengthInSamples; out.sampleRate = reader->sampleRate;
      juce::AudioBuffer<float> buffer(out.channels, out.numFrames);
      if (!reader->read(&buffer, 0, out.numFrames, 0, true, true)) { reader.reset(); cleanupTemporary(); error = "audio decode failed"; return false; }
      out.channelData.resize((size_t)out.channels * out.numFrames);
      for (int ch = 0; ch < out.channels; ++ch)
         std::copy(buffer.getReadPointer(ch), buffer.getReadPointer(ch) + out.numFrames, out.channelData.begin() + (size_t)ch * out.numFrames);
      reader.reset();
      cleanupTemporary();
      return true;
   }

   void AudioInputCaptureAddRef() { AudioBridgeInstance().inputRefs.fetch_add(1); }
   void AudioInputCaptureRemoveRef() { int v = AudioBridgeInstance().inputRefs.fetch_sub(1); if (v <= 1) AudioBridgeInstance().inputRefs.store(0); }
   void AudioInputCapturePump(std::string& error) { if (AudioBridgeInstance().inputRefs.load() > 0) AudioBridgeInstance().open(2, 2, error); }
   bool AudioInputCaptureIsRunning() { return AudioBridgeInstance().inputRefs.load() > 0 && AudioBridgeInstance().manager.getCurrentAudioDevice() != nullptr; }
   int AudioInputCaptureRead(float* const* out, int frames, int maxChannels) { return AudioBridgeInstance().inputRing.read(out, frames, maxChannels); }

   namespace
   {
      struct MidiKey { MidiDeviceId dev; int ch, ctl; bool note; bool operator<(const MidiKey& o) const { return std::tie(dev,ch,ctl,note) < std::tie(o.dev,o.ch,o.ctl,o.note); } };
      struct MidiState final : juce::MidiInputCallback
      {
         std::vector<std::unique_ptr<juce::MidiInput>> inputs;
         std::map<MidiDeviceId, std::string> names;
         // Built before any input starts, read-only while they run.
         std::map<const juce::MidiInput*, MidiDeviceId> ids;
         std::map<MidiKey, float> values;
         std::map<MidiKey, unsigned int> hits;
         std::map<std::pair<MidiDeviceId,int>, MidiLastNote> lastNotes;
         std::mutex mutex;
         MidiCCValue lastTouched; bool touched = false;
         static constexpr size_t cap = 4096;
         MidiNoteMessage noteRing[cap]; std::atomic<unsigned long long> noteWrite {0};
         std::atomic<long long> lastClockUs {0}; std::atomic<float> clockBpm {0.0f};

         void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg) override
         {
            MidiDeviceId dev = 0;
            {
               auto found = ids.find(source);
               dev = found != ids.end() ? found->second : HashId(source ? source->getName().toStdString() : "MIDI");
            }
            const int ch = std::max(0, msg.getChannel() - 1);
            if (msg.isController() || msg.isNoteOnOrOff())
            {
               const bool note = msg.isNoteOnOrOff();
               const int ctl = note ? msg.getNoteNumber() : msg.getControllerNumber();
               const float value = note ? (msg.isNoteOn() ? msg.getFloatVelocity() : 0.0f) : msg.getControllerValue() / 127.0f;
               std::lock_guard<std::mutex> lock(mutex);
               values[{dev,ch,ctl,note}] = value;
               lastTouched = {dev,ch,ctl,note,value}; touched = true;
               if (note)
               {
                  if (msg.isNoteOn())
                  {
                     unsigned int seq = ++hits[{dev,ch,ctl,true}];
                     lastNotes[{dev,ch}] = {ctl,value,seq};
                  }
                  const auto pos = noteWrite.load(std::memory_order_relaxed);
                  noteRing[pos % cap] = {dev,ch,ctl,value,msg.isNoteOn()};
                  noteWrite.store(pos + 1, std::memory_order_release);
               }
            }
            if (msg.isMidiClock())
            {
               const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
               const auto prev = lastClockUs.exchange(now);
               if (prev > 0 && now > prev)
               {
                  const float bpm = (float)(60000000.0 / ((now - prev) * 24.0));
                  const float old = clockBpm.load(); if (bpm > 20 && bpm < 400) clockBpm.store(old == 0 ? bpm : old * 0.9f + bpm * 0.1f);
               }
            }
         }
      };

      MidiState& MidiStateInstance()
      {
         EnsureJuceInitialised();
         static MidiState state;
         return state;
      }
   }

   bool MidiStart(std::string& error)
   {
      EnsureJuceInitialised();
      MidiState& st = MidiStateInstance();
      if (!st.inputs.empty()) return true;
      // Identical controllers share a name; the 2nd, 3rd... get "name #2",
      // "name #3" so every source has its own id (and MIDI learn never
      // confuses them). Open everything first, then start, so the id map is
      // complete before the first callback can read it.
      std::map<std::string, int> seen;
      for (const auto& d : juce::MidiInput::getAvailableDevices())
      {
         auto input = juce::MidiInput::openDevice(d.identifier, &st);
         if (!input) continue;
         const std::string name = d.name.toStdString();
         const int n = ++seen[name];
         const std::string key = n == 1 ? name : name + " #" + std::to_string(n);
         const MidiDeviceId id = HashId(key);
         st.names[id] = key;
         st.ids[input.get()] = id;
         st.inputs.push_back(std::move(input));
      }
      for (auto& input : st.inputs) input->start();
      if (st.inputs.empty()) { error = "no MIDI input devices found"; return false; }
      return true;
   }
   void MidiStop() { for (auto& i : MidiStateInstance().inputs) if (i) i->stop(); MidiStateInstance().inputs.clear(); MidiStateInstance().ids.clear(); }
   std::vector<MidiDeviceInfo> MidiDevices()
   {
      std::vector<MidiDeviceInfo> out;
      if (MidiStateInstance().inputs.empty()) return out;
      for (const auto& n : MidiStateInstance().names) out.push_back({ n.first, n.second });
      std::sort(out.begin(), out.end(), [](const MidiDeviceInfo& a, const MidiDeviceInfo& b) { return a.key < b.key; });
      return out;
   }
   bool MidiRescan(std::string& error)
   {
      MidiStop();
      MidiStateInstance().names.clear();
      return MidiStart(error);
   }
   bool MidiIsRunning() { return !MidiStateInstance().inputs.empty(); }
   std::string MidiDeviceSummary() { std::ostringstream s; bool first=true; for (const auto& n : MidiStateInstance().names) { if(!first)s<<", "; s<<n.second; first=false; } return s.str(); }
   std::string MidiDeviceName(MidiDeviceId id) { auto it=MidiStateInstance().names.find(id); return it==MidiStateInstance().names.end()?std::string():it->second; }
   bool MidiRead(MidiDeviceId dev,int ch,int ctl,bool note,float& v) { std::lock_guard<std::mutex> l(MidiStateInstance().mutex); auto it=MidiStateInstance().values.find({dev,ch,ctl,note}); if(it==MidiStateInstance().values.end())return false; v=it->second; return true; }
   bool MidiPollLastTouched(MidiCCValue& v) { std::lock_guard<std::mutex> l(MidiStateInstance().mutex); if(!MidiStateInstance().touched)return false; v=MidiStateInstance().lastTouched; MidiStateInstance().touched=false; return true; }
   unsigned int MidiNoteHitCount(MidiDeviceId d,int c,int n) { std::lock_guard<std::mutex> l(MidiStateInstance().mutex); return MidiStateInstance().hits[{d,c,n,true}]; }
   bool MidiChannelLastNote(MidiDeviceId d,int c,MidiLastNote& out) { std::lock_guard<std::mutex> l(MidiStateInstance().mutex); auto it=MidiStateInstance().lastNotes.find({d,c}); if(it==MidiStateInstance().lastNotes.end())return false; out=it->second; return true; }
   int MidiReadNotesSince(unsigned long long& cursor, MidiNoteMessage* out, int maxCount)
   {
      const auto w=MidiStateInstance().noteWrite.load(std::memory_order_acquire); if(cursor==0)cursor=w; if(w-cursor>MidiState::cap)cursor=w-MidiState::cap;
      int n=0; while(cursor<w&&n<maxCount){out[n++]=MidiStateInstance().noteRing[cursor%MidiState::cap];++cursor;} return n;
   }
   unsigned long long MidiNoteStreamPosition(){return MidiStateInstance().noteWrite.load(std::memory_order_acquire);}
   bool MidiClockIsPresent(){const auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); return now-MidiStateInstance().lastClockUs.load()<2000000;}
   float MidiClockBpm(){return MidiClockIsPresent()?MidiStateInstance().clockBpm.load():0.0f;}

   struct RecorderHandle
   {
      struct FramePacket
      {
         std::vector<unsigned char> pixels;
         int repeatCount = 1;
      };

      std::string finalPath, videoPath, audioPath;
      cv::VideoWriter video;
      AudioFileWriter audio;
      int width = 0, height = 0, fps = 30;
      bool hasLiveAudio = false;
      bool loopAudio = true;
      int liveAudioChannels = 2;

      // OpenCV's H.264 writer performs codec discovery, allocation and colour
      // conversion on its caller. Doing that from the render loop caused the
      // first seconds of a 1080p recording to miss their frame budget. The UI
      // is now a producer and this worker is the only encoder consumer.
      std::thread worker;
      std::mutex frameMutex;
      std::condition_variable frameReady;
      std::deque<FramePacket> frameQueue;
      std::deque<std::vector<float>> audioQueue;
      std::deque<std::vector<unsigned char>> freeFrameBuffers;
      size_t maxQueuedFrames = 8;
      size_t maxQueuedAudioBlocks = 256;
      bool stopRequested = false;
      std::atomic<bool> encoderFailed { false };
      std::atomic<int> submittedFrames { 0 };
      std::atomic<int> writtenFrames { 0 };
      std::atomic<int> droppedFrames { 0 };
      std::atomic<long long> droppedAudioFrames { 0 };
      std::string encoderError;
   };

   void RecorderVideoWorker(RecorderHandle* r)
   {
      bool opened = r->video.open(r->videoPath, cv::VideoWriter::fourcc('a','v','c','1'),
                                  r->fps, {r->width, r->height}, true);
      if (!opened)
         opened = r->video.open(r->videoPath, cv::VideoWriter::fourcc('m','p','4','v'),
                                r->fps, {r->width, r->height}, true);
      if (!opened)
      {
         {
            std::lock_guard<std::mutex> lock(r->frameMutex);
            r->encoderError = "could not create MP4 writer";
            r->frameQueue.clear();
            r->audioQueue.clear();
         }
         r->encoderFailed.store(true, std::memory_order_release);
         r->frameReady.notify_all();
         return;
      }

      cv::Mat top;
      cv::Mat bgr;
      top.create(r->height, r->width, CV_8UC4);
      bgr.create(r->height, r->width, CV_8UC3);
      for (;;)
      {
         RecorderHandle::FramePacket frame;
         std::vector<float> audio;
         bool hasFrame = false;
         {
            std::unique_lock<std::mutex> lock(r->frameMutex);
            r->frameReady.wait(lock, [r]
            {
               return r->stopRequested || !r->frameQueue.empty() || !r->audioQueue.empty();
            });
            if (r->frameQueue.empty() && r->audioQueue.empty())
            {
               if (r->stopRequested)
                  break;
               continue;
            }
            // Drain one audio block and one video packet per wake. File I/O,
            // colour conversion and codec work all remain on this worker.
            if (!r->audioQueue.empty())
            {
               audio = std::move(r->audioQueue.front());
               r->audioQueue.pop_front();
            }
            if (!r->frameQueue.empty())
            {
               frame = std::move(r->frameQueue.front());
               r->frameQueue.pop_front();
               hasFrame = true;
            }
         }

         if (!audio.empty() && r->hasLiveAudio)
            r->audio.Append(audio.data(), (int)(audio.size() / std::max(1, r->liveAudioChannels)));

         if (hasFrame)
         {
            cv::Mat rgba(r->height, r->width, CV_8UC4, frame.pixels.data());
            cv::flip(rgba, top, 0);
            cv::cvtColor(top, bgr, cv::COLOR_RGBA2BGR);
            const int repeats = std::max(1, frame.repeatCount);
            for (int i = 0; i < repeats; ++i)
               r->video.write(bgr);
            r->writtenFrames.fetch_add(repeats, std::memory_order_relaxed);
            {
               std::lock_guard<std::mutex> lock(r->frameMutex);
               if (r->freeFrameBuffers.size() < 8)
                  r->freeFrameBuffers.emplace_back(std::move(frame.pixels));
            }
         }
      }
      r->video.release();
   }

   RecorderHandle* RecorderStart(const std::string& path,int w,int h,int fps,std::string& error,
      const std::string& audioPath,bool loopAudio,double liveRate,int liveChannels)
   {
      if(path.empty()){error="choose where to save the video first";return nullptr;}
      auto r=std::make_unique<RecorderHandle>(); r->finalPath=path; r->videoPath=path+".video.tmp.mp4"; r->audioPath=audioPath; r->loopAudio=loopAudio; r->width=w; r->height=h; r->fps=std::max(1,fps);
      // About 256 MB at most: 32 queued 1080p RGBA frames, enough to absorb
      // codec warm-up without allowing an unbounded take to consume RAM.
      const size_t bytesPerFrame=(size_t)w*h*4;
      r->maxQueuedFrames=std::max<size_t>(8,std::min<size_t>(60,(256ull*1024ull*1024ull)/std::max<size_t>(1,bytesPerFrame)));
      for(int i=0;i<8;++i)r->freeFrameBuffers.emplace_back(bytesPerFrame);
      if(liveRate>0){r->hasLiveAudio=true;r->loopAudio=false;r->liveAudioChannels=std::max(1,liveChannels);r->audioPath=path+".audio.tmp.wav";if(!r->audio.Open(r->audioPath,liveRate,r->liveAudioChannels)){error="could not create temporary audio track";return nullptr;}}
      r->worker=std::thread(RecorderVideoWorker,r.get());
      return r.release();
   }
   std::vector<unsigned char> RecorderAcquireFrameBuffer(RecorderHandle* r)
   {
      if(!r)return {};
      std::vector<unsigned char> result;
      {
         std::lock_guard<std::mutex> lock(r->frameMutex);
         if(!r->freeFrameBuffers.empty())
         {
            result=std::move(r->freeFrameBuffers.front());
            r->freeFrameBuffers.pop_front();
         }
      }
      const size_t required=(size_t)r->width*r->height*4;
      if(result.size()!=required)result.resize(required);
      return result;
   }
   bool RecorderAppend(RecorderHandle* r,std::vector<unsigned char>&& px,int repeatCount)
   {
      if(!r||px.size()<(size_t)r->width*r->height*4||r->encoderFailed.load(std::memory_order_acquire))return false;
      {
         std::lock_guard<std::mutex> lock(r->frameMutex);
         if(r->stopRequested||r->encoderFailed.load(std::memory_order_relaxed))return false;
         if(r->frameQueue.size()>=r->maxQueuedFrames)
         {
            r->droppedFrames.fetch_add(std::max(1,repeatCount),std::memory_order_relaxed);
            if(r->freeFrameBuffers.size()<8)r->freeFrameBuffers.emplace_back(std::move(px));
            return false;
         }
         RecorderHandle::FramePacket packet;
         packet.pixels=std::move(px);
         packet.repeatCount=std::max(1,repeatCount);
         r->submittedFrames.fetch_add(packet.repeatCount,std::memory_order_relaxed);
         r->frameQueue.emplace_back(std::move(packet));
      }
      r->frameReady.notify_one();
      return true;
   }
   bool RecorderAppendAudio(RecorderHandle* r,const float* samples,int frames)
   {
      if(!r||!r->hasLiveAudio||!samples||frames<=0)return false;
      std::vector<float> block((size_t)frames*r->liveAudioChannels);
      std::copy(samples,samples+block.size(),block.begin());
      {
         std::lock_guard<std::mutex> lock(r->frameMutex);
         if(r->stopRequested||r->encoderFailed.load(std::memory_order_relaxed))return false;
         if(r->audioQueue.size()>=r->maxQueuedAudioBlocks)
         {
            r->droppedAudioFrames.fetch_add(frames,std::memory_order_relaxed);
            return false;
         }
         r->audioQueue.emplace_back(std::move(block));
      }
      r->frameReady.notify_one();
      return true;
   }
   bool RecorderStop(RecorderHandle* r,std::string& error)
   {
      if(!r)return false;
      {
         std::lock_guard<std::mutex> lock(r->frameMutex);
         r->stopRequested=true;
      }
      r->frameReady.notify_all();
      if(r->worker.joinable())r->worker.join();
      r->audio.Close();
      std::error_code ec;
      if(r->encoderFailed.load(std::memory_order_acquire))
      {
         error=r->encoderError.empty()?"video encoder failed":r->encoderError;
         fs::remove(fs::u8path(r->videoPath),ec);if(r->hasLiveAudio)fs::remove(fs::u8path(r->audioPath),ec);delete r;return false;
      }
      if(r->audioPath.empty()){fs::remove(fs::u8path(r->finalPath),ec);fs::rename(fs::u8path(r->videoPath),fs::u8path(r->finalPath),ec);if(ec)error=ec.message();delete r;return !ec;}
      const std::wstring loop = r->loopAudio ? L" -stream_loop -1" : L"";
      const std::wstring cmd=L"\""+FfmpegExecutable()+L"\" -y -loglevel error -i \""+Utf8ToWide(r->videoPath)+L"\""+loop+L" -i \""+Utf8ToWide(r->audioPath)+L"\" -shortest -c:v copy -c:a aac -b:a 192k \""+Utf8ToWide(r->finalPath)+L"\"";
      bool ok=RunHiddenProcess(cmd);
      if(!ok)error="FFmpeg could not mux the audio track. Install FFmpeg or run install-dependencies.bat again.";
      fs::remove(fs::u8path(r->videoPath),ec);if(r->hasLiveAudio)fs::remove(fs::u8path(r->audioPath),ec);delete r;return ok;
   }
   int RecorderFrameCount(RecorderHandle* r){return r?r->submittedFrames.load(std::memory_order_relaxed):0;}
   int RecorderPendingFrameCount(RecorderHandle* r)
   {
      if(!r)return 0;std::lock_guard<std::mutex> lock(r->frameMutex);int pending=0;for(const auto& packet:r->frameQueue)pending+=std::max(1,packet.repeatCount);return pending;
   }
   int RecorderDroppedFrameCount(RecorderHandle* r){return r?r->droppedFrames.load(std::memory_order_relaxed):0;}
   MovieInfo InspectMovie(const std::string& path)
   {
      MovieInfo m;
      cv::VideoCapture c(path);
      m.hasVideo = c.isOpened();
      if (m.hasVideo)
      {
         const double fps = c.get(cv::CAP_PROP_FPS), frames = c.get(cv::CAP_PROP_FRAME_COUNT);
         m.duration = fps > 0.0 ? frames / fps : 0.0;
      }
      const std::wstring command = L"ffprobe.exe -v error -select_streams a:0 -show_entries stream=index -of csv=p=0 \"" + Utf8ToWide(path) + L"\"";
      if (FILE* pipe = _wpopen(command.c_str(), L"rt"))
      {
         wchar_t result[32] {};
         m.hasAudio = fgetws(result, 32, pipe) != nullptr;
         _pclose(pipe);
      }
      return m;
   }

   struct SyphonServerHandle
   {
#if INFINITE_ENABLE_SPOUT
      SpoutSender sender;
#endif
      std::string name;
   };
   struct SyphonClientHandle
   {
#if INFINITE_ENABLE_SPOUT
      SpoutReceiver receiver;
#endif
      GLuint rectTexture=0;int width=0,height=0;
   };
   SyphonServerHandle* SyphonServerCreate(const std::string& name){auto*h=new SyphonServerHandle();h->name=name;
#if INFINITE_ENABLE_SPOUT
      h->sender.SetSenderName(name.c_str());
#endif
      return h;}
   void SyphonServerUpdateName(SyphonServerHandle*h,const std::string& n){if(!h)return;h->name=n;
#if INFINITE_ENABLE_SPOUT
      h->sender.ReleaseSender();h->sender.SetSenderName(n.c_str());
#endif
   }
   void SyphonServerPublish(SyphonServerHandle*h,unsigned int tex,int w,int hgt,bool flipped){if(!h)return;
#if INFINITE_ENABLE_SPOUT
      h->sender.SendTexture(tex,GL_TEXTURE_2D,w,hgt,!flipped);
#endif
   }
   bool SyphonServerHasClients(SyphonServerHandle*h){
#if INFINITE_ENABLE_SPOUT
      return h&&h->sender.IsInitialized();
#else
      return false;
#endif
   }
   void SyphonServerDestroy(SyphonServerHandle*h){if(!h)return;
#if INFINITE_ENABLE_SPOUT
      h->sender.ReleaseSender();
#endif
      delete h;}
   std::vector<SyphonServerInfo> SyphonGetAvailableServers(){std::vector<SyphonServerInfo> out;
#if INFINITE_ENABLE_SPOUT
      SpoutReceiver r;
      const int count = r.GetSenderCount();
      for(int i = 0; i < count; ++i){char name[256] {};if(r.GetSender(i,name,256))out.push_back({"Spout",name,name});}
#endif
      return out;}
   SyphonClientHandle* SyphonClientCreate(){return new SyphonClientHandle();}
   bool SyphonClientConnect(SyphonClientHandle*h,const std::string&,const std::string& name,const std::string&){if(!h)return false;
#if INFINITE_ENABLE_SPOUT
      h->receiver.ReleaseReceiver();h->receiver.SetReceiverName(name.c_str());return true;
#else
      return false;
#endif
   }
   bool SyphonClientIsConnected(SyphonClientHandle*h){
#if INFINITE_ENABLE_SPOUT
      return h&&h->receiver.IsConnected();
#else
      return false;
#endif
   }
   bool SyphonClientHasNewFrame(SyphonClientHandle*h){
#if INFINITE_ENABLE_SPOUT
      return h&&h->receiver.IsFrameNew();
#else
      return false;
#endif
   }
   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle*h,int& w,int& hgt){if(!h)return 0;
#if INFINITE_ENABLE_SPOUT
      if(!h->receiver.ReceiveTexture())return 0;w=(int)h->receiver.GetSenderWidth();hgt=(int)h->receiver.GetSenderHeight();
      if(w<=0||hgt<=0)return 0;if(h->rectTexture==0||w!=h->width||hgt!=h->height){if(h->rectTexture)glDeleteTextures(1,&h->rectTexture);glGenTextures(1,&h->rectTexture);glBindTexture(GL_TEXTURE_RECTANGLE,h->rectTexture);glTexParameteri(GL_TEXTURE_RECTANGLE,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_RECTANGLE,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_RECTANGLE,0,GL_RGBA8,w,hgt,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);h->width=w;h->height=hgt;}
      h->receiver.ReceiveTexture(h->rectTexture,GL_TEXTURE_RECTANGLE,false);return h->rectTexture;
#else
      w=hgt=0;return 0;
#endif
   }
   void SyphonClientDestroy(SyphonClientHandle*h){if(!h)return;
#if INFINITE_ENABLE_SPOUT
      h->receiver.ReleaseReceiver();
#endif
      if(h->rectTexture)glDeleteTextures(1,&h->rectTexture);delete h;}

   void InitDocumentHandlingPreGlfw() {}
   void InitDocumentHandlingPostGlfw() {}
   bool PollPendingOpenFile(std::string&) { return false; }

   struct CameraHandle
   {
      cv::VideoCapture capture;
      std::atomic<bool> running{false};
      std::atomic<bool> mirror{false};
      CameraResolution resolution = CameraResolution::Auto;
      std::thread worker;
      std::mutex frameMutex;
      std::vector<unsigned char> latestFrame;
      int width = 0;
      int height = 0;
      unsigned long long seq = 0;
      unsigned long long deliveredSeq = 0;
   };

   std::vector<CameraDeviceInfo> CameraListDevices()
   {
      // Opening every possible camera index just to build a menu can block the
      // UI for several seconds. DirectShow exposes the same list as metadata,
      // so enumerate monikers without starting any capture device.
      std::vector<CameraDeviceInfo> out;
      const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      ICreateDevEnum* devEnum = nullptr;
      IEnumMoniker* enumMoniker = nullptr;
      if (SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ICreateDevEnum, (void**)&devEnum)) &&
          devEnum != nullptr &&
          devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0) == S_OK)
      {
         IMoniker* moniker = nullptr;
         int index = 0;
         while (enumMoniker->Next(1, &moniker, nullptr) == S_OK)
         {
            std::string name = "Camera " + std::to_string(index);
            IPropertyBag* bag = nullptr;
            if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&bag)) && bag != nullptr)
            {
               VARIANT value;
               VariantInit(&value);
               if (SUCCEEDED(bag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR)
                  name = WideToUtf8(std::wstring(value.bstrVal, SysStringLen(value.bstrVal)));
               VariantClear(&value);
               bag->Release();
            }
            out.push_back({std::to_string(index), name, index == 0});
            ++index;
            moniker->Release();
         }
      }
      if (enumMoniker != nullptr) enumMoniker->Release();
      if (devEnum != nullptr) devEnum->Release();
      if (SUCCEEDED(init)) CoUninitialize();
      RuntimeLog::Write("camera enumeration: %d device(s)", (int)out.size());
      return out;
   }

   void CameraSetResolution(CameraHandle* h, CameraResolution r)
   {
      if (h == nullptr) return;
      h->resolution = r;
      int w = 0, ht = 0;
      if (r == CameraResolution::Res1080p) { w = 1920; ht = 1080; }
      else if (r == CameraResolution::Res720p) { w = 1280; ht = 720; }
      else if (r == CameraResolution::Res480p) { w = 640; ht = 480; }
      if (w != 0)
      {
         // Most USB webcams only reach 30 fps at 720p/1080p in MJPG; the
         // default YUY2 mode often drops to 5-10 fps. Ignored if unsupported.
         h->capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
         h->capture.set(cv::CAP_PROP_FRAME_WIDTH, w);
         h->capture.set(cv::CAP_PROP_FRAME_HEIGHT, ht);
      }
   }

   CameraHandle* CameraOpen(const std::string& id, CameraResolution res, bool mirror, std::string& error)
   {
      auto h = std::make_unique<CameraHandle>();
      const int index = id.empty() ? 0 : std::atoi(id.c_str());
      if (!h->capture.open(index, cv::CAP_DSHOW))
      {
         error = "could not open camera";
         RuntimeLog::Write("camera open failed: index=%d", index);
         return nullptr;
      }
      h->mirror.store(mirror, std::memory_order_relaxed);
      h->capture.set(cv::CAP_PROP_BUFFERSIZE, 1); // newest frame, not a queue
      CameraSetResolution(h.get(), res);
      h->running.store(true, std::memory_order_release);
      CameraHandle* raw = h.get();
      h->worker = std::thread([raw]() {
         cv::Mat frame;
         std::vector<unsigned char> pixels;
         int consecutiveFailures = 0;
         while (raw->running.load(std::memory_order_acquire))
         {
            if (!raw->capture.read(frame) || frame.empty())
            {
               if (++consecutiveFailures == 120)
                  RuntimeLog::Write("camera capture is not delivering frames");
               std::this_thread::sleep_for(std::chrono::milliseconds(5));
               continue;
            }
            consecutiveFailures = 0;
            if (frame.type() != CV_8UC3)
            {
               cv::Mat converted;
               if (frame.channels() == 4) cv::cvtColor(frame, converted, cv::COLOR_BGRA2BGR);
               else if (frame.channels() == 1) cv::cvtColor(frame, converted, cv::COLOR_GRAY2BGR);
               else frame.convertTo(converted, CV_8UC3);
               frame = converted;
            }
            // One pass: vertical flip to GL row order, plus the horizontal
            // mirror when requested (flip code -1 = both axes). Stays BGR8;
            // the GPU swizzles on upload.
            pixels.resize((size_t)frame.cols * (size_t)frame.rows * 3);
            cv::Mat dst(frame.rows, frame.cols, CV_8UC3, pixels.data());
            cv::flip(frame, dst, raw->mirror.load(std::memory_order_relaxed) ? -1 : 0);
            std::lock_guard<std::mutex> lock(raw->frameMutex);
            raw->width = frame.cols;
            raw->height = frame.rows;
            raw->latestFrame.swap(pixels);
            ++raw->seq;
         }
      });
      RuntimeLog::Write("camera opened: index=%d", index);
      return h.release();
   }

   void CameraClose(CameraHandle* h)
   {
      if (h == nullptr) return;
      h->running.store(false, std::memory_order_release);
      if (h->worker.joinable()) h->worker.join();
      h->capture.release();
      RuntimeLog::Write("camera closed");
      delete h;
   }

   bool CameraIsRunning(CameraHandle* h)
   {
      return h != nullptr && h->running.load(std::memory_order_acquire);
   }

   void CameraSetMirror(CameraHandle* h, bool mirror)
   {
      if (h != nullptr) h->mirror.store(mirror, std::memory_order_relaxed);
   }

   bool CameraReadFrame(CameraHandle* h, std::vector<unsigned char>& out, int& w, int& ht,
                        unsigned long long& seq)
   {
      if (h == nullptr) return false;
      std::lock_guard<std::mutex> lock(h->frameMutex);
      if (h->latestFrame.empty() || h->seq == h->deliveredSeq) return false;
      out.swap(h->latestFrame); // recycled by the capture thread
      w = h->width;
      ht = h->height;
      seq = h->seq;
      h->deliveredSeq = h->seq;
      return true;
   }
}

