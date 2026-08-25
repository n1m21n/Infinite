#include "Platform.h"

#if defined(_WIN32)

#include <windows.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <exception>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "platform/SettingsPaths.h"

namespace fs = std::filesystem;

namespace Platform
{
   void EnsureJuceInitialised();

   namespace
   {
      juce::VST3PluginFormat& VstFormat()
      {
         EnsureJuceInitialised();
         static juce::VST3PluginFormat format;
         return format;
      }

      std::mutex gPathMutex;
      std::unordered_map<std::string, std::string> gPathByIdentifier;
      std::vector<std::string> gSearchFolders;
      std::vector<std::string> gScanFailures;
      std::mutex gBlocklistMutex;
      std::vector<std::string> gBlocklist;
      bool gBlocklistLoaded = false;
      std::atomic<bool> gScanChild { false };
      std::atomic<int> gOpenEditorCount { 0 };
      std::mutex gVstLogMutex;

      // v1/v2 were populated while scans launched the full Infinite app as the
      // child process. v3 belongs to the dedicated minimal scanner helper, so
      // every bundle gets one clean retry under the corrected architecture.
      fs::path BlocklistPath() { return fs::u8path(InfiniteSettingsDirectory()) / "PluginVST3Blocklist-v3.txt"; }

      void VstLog(const std::string& message)
      {
         std::lock_guard<std::mutex> lock(gVstLogMutex);
         const fs::path path = fs::u8path(InfiniteSettingsDirectory()) / "Infinite-vst3.log";
         std::ofstream file(path, std::ios::app);
         if (!file) return;
         SYSTEMTIME now {};
         GetLocalTime(&now);
         file << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' '
              << now.wHour << ':' << now.wMinute << ':' << now.wSecond << "  "
              << message << '\n';
      }

      void LoadBlocklistLocked()
      {
         if (gBlocklistLoaded) return;
         gBlocklistLoaded = true;
         std::ifstream file { BlocklistPath() };
         std::string line;
         while (std::getline(file, line)) if (!line.empty()) gBlocklist.push_back(line);
      }

      void SaveBlocklistLocked()
      {
         std::ofstream file { BlocklistPath(), std::ios::trunc };
         for (const auto& path : gBlocklist) file << path << '\n';
      }

      bool IsBlocklisted(const std::string& path)
      {
         std::lock_guard<std::mutex> lock(gBlocklistMutex);
         LoadBlocklistLocked();
         return std::find(gBlocklist.begin(), gBlocklist.end(), path) != gBlocklist.end();
      }

      void AddToBlocklist(const std::string& path)
      {
         std::lock_guard<std::mutex> lock(gBlocklistMutex);
         LoadBlocklistLocked();
         if (std::find(gBlocklist.begin(), gBlocklist.end(), path) == gBlocklist.end())
         {
            gBlocklist.push_back(path);
            SaveBlocklistLocked();
         }
      }

      std::string PluginIdentifier(const juce::PluginDescription& d)
      {
         return "vst3:" + d.createIdentifierString().toStdString();
      }

      PluginDesc ToPlatformDesc(const juce::PluginDescription& d)
      {
         PluginDesc out;
         out.format = "vst3";
         out.name = d.name.toStdString();
         out.manufacturer = d.manufacturerName.toStdString();
         out.identifier = PluginIdentifier(d);
         out.path = d.fileOrIdentifier.toStdString();
         out.acceptsNotes = d.isInstrument || d.numInputChannels == 0;
         return out;
      }

      bool DescribePath(const std::string& path, std::vector<PluginDesc>& out)
      {
         juce::OwnedArray<juce::PluginDescription> types;
         VstFormat().findAllTypesForFile(types, juce::String::fromUTF8(path.c_str()));
         for (auto* type : types)
         {
            if (type == nullptr)
               continue;
            PluginDesc desc = ToPlatformDesc(*type);
            out.push_back(desc);
            std::lock_guard<std::mutex> lock(gPathMutex);
            gPathByIdentifier[desc.identifier] = desc.path;
         }
         return !types.isEmpty();
      }

      void AddVstBundlesFromFolder(const std::string& folder, std::vector<std::string>& paths)
      {
         std::error_code ec;
         if (!fs::exists(fs::u8path(folder), ec))
            return;
         for (fs::recursive_directory_iterator it(fs::u8path(folder), fs::directory_options::skip_permission_denied, ec), end;
              it != end && !ec; it.increment(ec))
         {
            if (!it->is_directory(ec))
               continue;
            std::string extension = it->path().extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (extension == ".vst3")
            {
               paths.push_back(it->path().u8string());
               it.disable_recursion_pending();
            }
         }
      }

      std::wstring Utf8ToWide(const std::string& text)
      {
         if (text.empty()) return {};
         const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
         std::wstring out((size_t)count, L'\0');
         MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), count);
         if (!out.empty()) out.pop_back();
         return out;
      }

      std::string WideToUtf8(const std::wstring& text)
      {
         if (text.empty()) return {};
         const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(),
                                                nullptr, 0, nullptr, nullptr);
         if (count <= 0) return {};
         std::string out((size_t)count, '\0');
         WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(),
                             out.data(), count, nullptr, nullptr);
         return out;
      }

      std::vector<PluginDesc> ParseProbeOutput(const std::string& output)
      {
         std::vector<PluginDesc> parsed;
         size_t position = 0;
         while (position < output.size())
         {
            const size_t newline = output.find('\n', position);
            std::string line = output.substr(position, newline == std::string::npos ? std::string::npos : newline - position);
            position = newline == std::string::npos ? output.size() : newline + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> fields;
            size_t fieldStart = 0;
            while (fieldStart <= line.size())
            {
               const size_t tab = line.find('\t', fieldStart);
               fields.push_back(line.substr(fieldStart, tab == std::string::npos ? std::string::npos : tab - fieldStart));
               if (tab == std::string::npos) break;
               fieldStart = tab + 1;
            }
            if (fields.size() != 6) continue;
            PluginDesc d;
            d.format = fields[0]; d.name = fields[1]; d.manufacturer = fields[2];
            d.identifier = fields[3]; d.path = fields[4]; d.acceptsNotes = fields[5] == "1";
            parsed.push_back(std::move(d));
         }
         return parsed;
      }

      enum class ProbeResult { Success, CleanMiss, Crashed, InfrastructureError };

      ProbeResult ProbeVstBundleOutOfProcess(const std::string& path, std::vector<PluginDesc>& out)
      {
         SECURITY_ATTRIBUTES security { sizeof(security), nullptr, TRUE };
         HANDLE readPipe = nullptr, writePipe = nullptr;
         if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return ProbeResult::InfrastructureError;
         SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
         HANDLE nullOutput = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

         STARTUPINFOW startup { sizeof(startup) };
         startup.dwFlags = STARTF_USESTDHANDLES;
         startup.hStdOutput = writePipe;
         startup.hStdError = nullOutput == INVALID_HANDLE_VALUE ? writePipe : nullOutput;
         startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
         PROCESS_INFORMATION process {};
         const std::string scannerPath = ScannerExecutablePath();
         if (!fs::exists(fs::u8path(scannerPath)))
         {
            VstLog("scanner helper missing: " + scannerPath);
            CloseHandle(writePipe);
            CloseHandle(readPipe);
            if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);
            return ProbeResult::InfrastructureError;
         }
         std::wstring command = L"\"" + Utf8ToWide(scannerPath) + L"\" --vst3-scan-bundle \"" + Utf8ToWide(path) + L"\"";
         std::vector<wchar_t> mutableCommand(command.begin(), command.end());
         mutableCommand.push_back(L'\0');
         const bool launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                              CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
         CloseHandle(writePipe);
         if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);
         if (!launched) { CloseHandle(readPipe); return ProbeResult::InfrastructureError; }

         std::string output;
         char buffer[4096];
         DWORD count = 0;
         DWORD wait = WAIT_TIMEOUT;
         const ULONGLONG deadline = GetTickCount64() + 30000;
         while (GetTickCount64() < deadline)
         {
            DWORD available = 0;
            if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
            {
               const DWORD wanted = std::min<DWORD>((DWORD)sizeof(buffer), available);
               if (ReadFile(readPipe, buffer, wanted, &count, nullptr) && count > 0)
                  output.append(buffer, buffer + count);
            }
            wait = WaitForSingleObject(process.hProcess, 10);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED) break;
         }
         if (wait != WAIT_OBJECT_0)
         {
            TerminateProcess(process.hProcess, 0xDEAD);
            WaitForSingleObject(process.hProcess, 2000);
         }
         while (ReadFile(readPipe, buffer, sizeof(buffer), &count, nullptr) && count > 0)
            output.append(buffer, buffer + count);
         CloseHandle(readPipe);
         DWORD exitCode = 1;
         GetExitCodeProcess(process.hProcess, &exitCode);
         CloseHandle(process.hThread);
         CloseHandle(process.hProcess);
         if (wait != WAIT_OBJECT_0 || exitCode != 0)
         {
            VstLog("scanner exit " + std::to_string(exitCode) + " for " + path);
            return ProbeResult::Crashed;
         }
         auto parsed = ParseProbeOutput(output);
         for (auto& d : parsed)
         {
            std::lock_guard<std::mutex> lock(gPathMutex);
            gPathByIdentifier[d.identifier] = d.path;
            out.push_back(std::move(d));
         }
         return parsed.empty() ? ProbeResult::CleanMiss : ProbeResult::Success;
      }

      juce::PluginDescription FindDescription(const PluginDesc& desc, bool& found)
      {
         found = false;
         std::string path = desc.path;
         if (path.empty())
         {
            std::lock_guard<std::mutex> lock(gPathMutex);
            auto it = gPathByIdentifier.find(desc.identifier);
            if (it != gPathByIdentifier.end())
               path = it->second;
         }
         if (path.empty())
            return {};

         juce::OwnedArray<juce::PluginDescription> types;
         VstFormat().findAllTypesForFile(types, juce::String::fromUTF8(path.c_str()));
         for (auto* type : types)
         {
            if (type == nullptr)
               continue;
            if (desc.identifier.empty() || PluginIdentifier(*type) == desc.identifier)
            {
               found = true;
               return *type;
            }
         }
         return {};
      }

      class EditorWindow final : public juce::DocumentWindow
      {
      public:
         EditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor,
                      std::function<void()> onClose)
            : DocumentWindow(name, juce::Colours::black, DocumentWindow::closeButton), callback(std::move(onClose))
         {
            setUsingNativeTitleBar(true);
            setContentOwned(editor, true);
            setResizable(true, false);
            centreWithSize(std::max(320, editor->getWidth()), std::max(200, editor->getHeight()));
            setVisible(true);
            toFront(true);
         }

         void closeButtonPressed() override
         {
            if (callback)
               callback();
         }

      private:
         std::function<void()> callback;
      };
   }

   struct PluginAsyncState
   {
      std::mutex mutex;
      PluginHandle* handle = nullptr;
   };

   struct PluginHandle final : public juce::AudioProcessorListener
   {
      PluginDesc desc;
      std::unique_ptr<juce::AudioPluginInstance> instance;
      std::unique_ptr<EditorWindow> editor;
      std::mutex loadMutex;
      std::string loadError;
      std::atomic<PluginLoadState> state { PluginLoadState::Pending };
      std::atomic<unsigned long long> learned { ~0ULL };
      std::atomic<bool> learnEnabled { false };
      std::atomic<bool> renderInFlight { false };
      std::atomic<bool> renderEnabled { true };
      juce::AudioBuffer<float> work;
      juce::MidiBuffer midi;
      double sampleRate = 44100.0;
      int maxFrames = 4096;
      int latencySamples = 0;
      std::atomic<uint64_t> rejectedBlocks { 0 };
      std::shared_ptr<PluginAsyncState> asyncState;

      void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override
      {
         if (learnEnabled.load(std::memory_order_relaxed))
            learned.store((unsigned long long)index + 1ULL, std::memory_order_release);
      }
      void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
   };

   void EnumerateAudioUnits(std::vector<PluginDesc>&) {}
   bool DescribeAudioUnitBundle(const std::string&, std::vector<PluginDesc>&) { return false; }

   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out)
   {
#if INFINITE_ENABLE_VST3
      gScanFailures.clear();
      std::vector<std::string> paths;
      for (const auto& folder : folders)
         AddVstBundlesFromFolder(folder, paths);
      std::sort(paths.begin(), paths.end());
      paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
      for (const auto& path : paths)
      {
         if (IsBlocklisted(path)) { gScanFailures.push_back(path); VstLog("scan skipped by blocklist: " + path); continue; }
         const ProbeResult result = ProbeVstBundleOutOfProcess(path, out);
         if (result == ProbeResult::Success) VstLog("scan success: " + path);
         else if (result == ProbeResult::CleanMiss) VstLog("scan found no usable VST3 class: " + path);
         else if (result == ProbeResult::InfrastructureError) VstLog("scan infrastructure error: " + path);
         else VstLog("scan child crashed, failed or timed out: " + path);
         if (result != ProbeResult::Success) gScanFailures.push_back(path);
         if (result == ProbeResult::Crashed) AddToBlocklist(path);
      }
#else
      (void)folders; (void)out;
#endif
   }

   std::string ExecutablePath()
   {
      std::wstring path(32768, L'\0');
      DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
      path.resize(n);
      return WideToUtf8(path);
   }

   std::string ScannerExecutablePath()
   {
      return (fs::u8path(ExecutablePath()).parent_path() / "infinite-vst3-scanner.exe").u8string();
   }
   void SuppressAppUIForScanChild() { gScanChild.store(true); EnsureJuceInitialised(); }
   bool DescribeVST3Bundle(const std::string& path, std::vector<PluginDesc>& out)
   {
      if (gScanChild.load()) return DescribePath(path, out);
      return ProbeVstBundleOutOfProcess(path, out) == ProbeResult::Success;
   }

   void CacheVST3BundlePath(const std::string& identifier, const std::string& path)
   {
      std::lock_guard<std::mutex> lock(gPathMutex);
      gPathByIdentifier[identifier] = path;
   }

   void SetVST3SearchFolders(const std::vector<std::string>& folders)
   {
      std::lock_guard<std::mutex> lock(gPathMutex);
      gSearchFolders = folders;
   }

   std::vector<std::string> VST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gBlocklistMutex);
      LoadBlocklistLocked();
      return gBlocklist;
   }
   void ClearVST3Blocklist()
   {
      std::lock_guard<std::mutex> lock(gBlocklistMutex);
      LoadBlocklistLocked();
      gBlocklist.clear();
      SaveBlocklistLocked();
   }
   std::vector<std::string> VST3ScanFailures() { return gScanFailures; }

   PluginHandle* PluginCreate(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
#if !INFINITE_ENABLE_VST3
      (void)desc; (void)sampleRate; (void)maxBlockFrames;
      return nullptr;
#else
      EnsureJuceInitialised();
      auto* handle = new PluginHandle();
      handle->asyncState = std::make_shared<PluginAsyncState>();
      handle->asyncState->handle = handle;
      handle->desc = desc;
      handle->sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
      handle->maxFrames = std::max(64, maxBlockFrames);

      bool found = false;
      juce::PluginDescription juceDesc = FindDescription(desc, found);
      if (!found)
      {
         handle->loadError = "VST3 plugin not found: " + desc.name;
         VstLog("load failed, description not found: " + desc.name + " | " + desc.path);
         handle->state.store(PluginLoadState::Failed, std::memory_order_release);
         return handle;
      }

      auto asyncState = handle->asyncState;
      VstFormat().createPluginInstanceAsync(juceDesc, handle->sampleRate, handle->maxFrames,
         [asyncState](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
         {
            std::lock_guard<std::mutex> lifeLock(asyncState->mutex);
            PluginHandle* handle = asyncState->handle;
            if (handle == nullptr)
               return;
            std::lock_guard<std::mutex> lock(handle->loadMutex);
            if (!instance)
            {
               handle->loadError = error.toStdString();
               VstLog("load failed: " + handle->desc.name + " | " + handle->loadError);
               handle->state.store(PluginLoadState::Failed, std::memory_order_release);
               return;
            }
            handle->instance = std::move(instance);
            handle->instance->addListener(handle);
            handle->instance->setNonRealtime(false);
            handle->instance->setRateAndBufferSizeDetails(handle->sampleRate, handle->maxFrames);
            handle->instance->prepareToPlay(handle->sampleRate, handle->maxFrames);
            const int channels = std::max(2, std::max(handle->instance->getTotalNumInputChannels(),
                                                      handle->instance->getTotalNumOutputChannels()));
            handle->work.setSize(channels, handle->maxFrames, false, true, false);
            handle->midi.ensureSize(4096);
            handle->latencySamples = handle->instance->getLatencySamples();
            VstLog("load success: " + handle->desc.name + " | " + handle->desc.path);
            handle->state.store(PluginLoadState::Ready, std::memory_order_release);
         });
      return handle;
#endif
   }

   PluginLoadState PluginPoll(PluginHandle* handle, std::string& outError)
   {
      if (!handle)
      {
         outError = "null plugin handle";
         return PluginLoadState::Failed;
      }
      PumpPluginEditorEvents();
      const auto state = handle->state.load(std::memory_order_acquire);
      if (state == PluginLoadState::Failed)
      {
         std::lock_guard<std::mutex> lock(handle->loadMutex);
         outError = handle->loadError;
      }
      return state;
   }

   bool PluginPrepare(PluginHandle* handle, double sampleRate, int maxBlockFrames, std::string& outError)
   {
      if (!handle || !handle->instance)
      {
         outError = "plugin is not ready";
         return false;
      }
      handle->renderEnabled.store(false, std::memory_order_release);
      // The audio half unpublishes the handle before this call, but a block
      // that already loaded the old pointer may still be inside processBlock.
      // Never release a plugin's resources under that callback.
      for (int i = 0; i < 250 && handle->renderInFlight.load(std::memory_order_acquire); ++i)
         Sleep(1);
      if (handle->renderInFlight.load(std::memory_order_acquire))
      {
         outError = "plugin render did not finish before prepare";
         VstLog("prepare timed out waiting for render: " + handle->desc.name);
         return false;
      }

      try
      {
         handle->sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
         handle->maxFrames = std::max(64, maxBlockFrames);
         handle->instance->releaseResources();
         handle->instance->setRateAndBufferSizeDetails(handle->sampleRate, handle->maxFrames);
         handle->instance->prepareToPlay(handle->sampleRate, handle->maxFrames);
         handle->instance->reset();
         const int channels = std::max(2, std::max(handle->instance->getTotalNumInputChannels(),
                                                   handle->instance->getTotalNumOutputChannels()));
         handle->work.setSize(channels, handle->maxFrames, false, true, false);
         handle->midi.clear();
         handle->latencySamples = handle->instance->getLatencySamples();
         handle->renderEnabled.store(true, std::memory_order_release);
         VstLog("prepared " + handle->desc.name + " at " + std::to_string(handle->sampleRate) +
                " Hz / " + std::to_string(handle->maxFrames) + " frames");
         return true;
      }
      catch (const std::exception& e)
      {
         outError = e.what();
      }
      catch (...)
      {
         outError = "unknown exception while preparing plugin";
      }
      VstLog("prepare failed: " + handle->desc.name + " | " + outError);
      return false;
   }

   void PluginDestroy(PluginHandle* handle)
   {
      if (!handle) return;
      handle->renderEnabled.store(false, std::memory_order_release);
      if (handle->asyncState)
      {
         std::lock_guard<std::mutex> lock(handle->asyncState->mutex);
         handle->asyncState->handle = nullptr;
      }
      PluginCloseEditor(handle);
      for (int i = 0; i < 100 && handle->renderInFlight.load(std::memory_order_acquire); ++i)
         Sleep(1);
      if (handle->instance)
      {
         handle->instance->removeListener(handle);
         handle->instance->releaseResources();
      }
      delete handle;
   }

   PluginDesc PluginDescriptionOf(PluginHandle* handle) { return handle ? handle->desc : PluginDesc{}; }
   int PluginLatencySamples(PluginHandle* handle) { return handle ? handle->latencySamples : 0; }

   void PluginRender(PluginHandle* handle, const float* const* in, int inChannels,
                     float* const* out, int outChannels, int numFrames)
   {
      if (!handle || !handle->instance || numFrames <= 0 || numFrames > handle->maxFrames)
      {
         for (int ch = 0; ch < outChannels; ++ch)
            if (out[ch]) std::fill(out[ch], out[ch] + std::max(0, numFrames), 0.0f);
         return;
      }

      auto passThrough = [&]()
      {
         for (int ch = 0; ch < outChannels; ++ch)
         {
            if (!out[ch]) continue;
            const float* src = (in && inChannels > 0) ? in[std::min(ch, inChannels - 1)] : nullptr;
            if (src) std::copy(src, src + numFrames, out[ch]);
            else std::fill(out[ch], out[ch] + numFrames, 0.0f);
         }
      };

      if (!handle->renderEnabled.load(std::memory_order_acquire))
      {
         passThrough();
         return;
      }
      struct RenderGuard
      {
         PluginHandle* h;
         explicit RenderGuard(PluginHandle* value) : h(value)
         {
            h->renderInFlight.store(true, std::memory_order_release);
         }
         ~RenderGuard() { h->renderInFlight.store(false, std::memory_order_release); }
      } guard(handle);
      // Close the small race where this callback read renderEnabled just
      // before the main thread disabled it and started waiting. Once the
      // guard is visible, prepare waits; if prepare already observed the
      // prior false state, this recheck guarantees we never touch instance.
      if (!handle->renderEnabled.load(std::memory_order_acquire))
      {
         passThrough();
         return;
      }

      try
      {
         juce::ScopedNoDenormals noDenormals;
         auto& work = handle->work;
         work.clear(0, numFrames);
         for (int ch = 0; ch < std::min(inChannels, work.getNumChannels()); ++ch)
            if (in && in[ch]) work.copyFrom(ch, 0, in[ch], numFrames);
         handle->instance->processBlock(work, handle->midi);
         handle->midi.clear();

         constexpr float kPluginSafetyCeiling = 16.0f;
         uint64_t rejected = 0;
         for (int ch = 0; ch < outChannels; ++ch)
         {
            if (!out[ch]) continue;
            if (ch >= work.getNumChannels())
            {
               std::fill(out[ch], out[ch] + numFrames, 0.0f);
               continue;
            }
            const float* src = work.getReadPointer(ch);
            for (int i = 0; i < numFrames; ++i)
            {
               float v = src[i];
               if (!std::isfinite(v))
               {
                  v = 0.0f;
                  ++rejected;
               }
               else if (v > kPluginSafetyCeiling || v < -kPluginSafetyCeiling)
               {
                  v = std::clamp(v, -kPluginSafetyCeiling, kPluginSafetyCeiling);
                  ++rejected;
               }
               out[ch][i] = v;
            }
         }
         if (rejected > 0)
         {
            handle->rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
            // Treat an invalid block as a temporary plugin bypass. This keeps
            // one misbehaving effect from muting or corrupting the entire
            // serial audio chain; instruments (no input) become silence for
            // that block and can recover on the next one.
            passThrough();
         }
      }
      catch (...)
      {
         handle->midi.clear();
         handle->rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
         passThrough();
      }
   }

   void PluginScheduleMIDIEvent(PluginHandle* handle, int frameOffset, const unsigned char* bytes, int byteCount)
   {
      if (!handle || !bytes || byteCount <= 0) return;
      handle->midi.addEvent(bytes, byteCount, std::max(0, frameOffset));
   }

   int PluginParameterCount(PluginHandle* handle)
   {
      return handle && handle->instance ? handle->instance->getParameters().size() : 0;
   }

   bool PluginParameterInfo(PluginHandle* handle, int index, PluginParamInfo& out)
   {
      if (!handle || !handle->instance || index < 0 || index >= handle->instance->getParameters().size()) return false;
      auto* p = handle->instance->getParameters()[index];
      if (!p) return false;
      out.address = (unsigned long long)index + 1ULL;
      out.displayName = p->getName(128).toStdString();
      out.minValue = 0.0f;
      out.maxValue = 1.0f;
      out.defaultValue = p->getDefaultValue();
      out.unit = p->getLabel().toStdString();
      return true;
   }

   bool PluginParameterInfoByAddress(PluginHandle* handle, unsigned long long address, PluginParamInfo& out)
   { return address > 0 && PluginParameterInfo(handle, (int)address - 1, out); }

   void PluginSetParameter(PluginHandle* handle, unsigned long long address, float value)
   {
      const int index = (int)address - 1;
      if (!handle || !handle->instance || index < 0 || index >= handle->instance->getParameters().size()) return;
      if (auto* p = handle->instance->getParameters()[index])
         p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
   }

   bool PluginGetParameter(PluginHandle* handle, unsigned long long address, float& outValue)
   {
      const int index = (int)address - 1;
      if (!handle || !handle->instance || index < 0 || index >= handle->instance->getParameters().size()) return false;
      if (auto* p = handle->instance->getParameters()[index]) { outValue = p->getValue(); return true; }
      return false;
   }

   void PluginBeginLearn(PluginHandle* handle) { if (handle) { handle->learned.store(~0ULL); handle->learnEnabled.store(true); } }
   void PluginEndLearn(PluginHandle* handle) { if (handle) handle->learnEnabled.store(false); }
   bool PluginPollLearned(PluginHandle* handle, unsigned long long& outAddress)
   {
      if (!handle) return false;
      const auto v = handle->learned.exchange(~0ULL, std::memory_order_acq_rel);
      if (v == ~0ULL) return false;
      outAddress = v;
      return true;
   }

   bool PluginOpenEditor(PluginHandle* handle, std::string& outError)
   {
      if (!handle || !handle->instance) { outError = "plugin is not ready"; return false; }
      if (handle->editor) { handle->editor->toFront(true); return true; }
      auto* editor = handle->instance->createEditorIfNeeded();
      if (!editor) { outError = "plugin has no editor"; return false; }
      handle->editor = std::make_unique<EditorWindow>(handle->desc.name, editor, [handle]() { PluginCloseEditor(handle); });
      gOpenEditorCount.fetch_add(1);
      return true;
   }

   void PluginCloseEditor(PluginHandle* handle)
   {
      if (!handle || !handle->editor) return;
      handle->editor.reset();
      gOpenEditorCount.fetch_sub(1);
   }
   bool PluginEditorIsOpen(PluginHandle* handle) { return handle && handle->editor != nullptr; }
   bool AnyPluginEditorOpen() { return gOpenEditorCount.load() > 0; }
   bool PumpPluginEditorEvents()
   {
      EnsureJuceInitialised();
      auto* mm = juce::MessageManager::getInstanceWithoutCreating();
      if (!mm || !mm->isThisTheMessageThread()) return false;
      mm->runDispatchLoopUntil(1);
      return true;
   }

   bool PluginSaveState(PluginHandle* handle, std::string& outBase64)
   {
      if (!handle || !handle->instance) return false;
      juce::MemoryBlock state;
      handle->instance->getStateInformation(state);
      if (state.getSize() == 0) return false;
      outBase64 = state.toBase64Encoding().toStdString();
      return true;
   }

   bool PluginRestoreState(PluginHandle* handle, const std::string& base64)
   {
      if (!handle || !handle->instance) return false;
      juce::MemoryBlock state;
      if (!state.fromBase64Encoding(juce::String::fromUTF8(base64.c_str()))) return false;
      handle->instance->setStateInformation(state.getData(), (int)state.getSize());
      return true;
   }
}

#endif
