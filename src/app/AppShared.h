#pragma once
// Shared declarations for the code split out of main.cpp.
#include "app/AppCommon.h"

namespace app
{
void PushUndoCheckpoint();

GraphNode* FindNodeByIndex(int index);

INode* FindHardwareDrivenNode();

void StartOfflineRenderSession(OutputNode* n, int width = 0, int height = 0, bool isArrange = false);

bool StartAudioEngine(std::string& outError);

extern std::vector<GraphNode> gNodes;

 // 0 = Modules, 1 = Samples, 2 = Media, 3 = Plugins, 4 = Field

   // Shared sort/filter state for the docked node-browser panel's control
   // strip (DrawBrowserFilterStrip, below DropdownButton). One instance per
   // mode, not one shared instance - same reasoning as the four independent
   // `static char` search buffers this replaces (see the comment at
   // DrawLibrarySearchPanel's cache setup): each mode keeps its own sort,
   // filter and in-progress query when the user switches tabs and back.
   // `sortMode` and `typeFilter` are indices into that mode's own option
   // list (see the per-mode DrawBrowserFilterStrip call sites) - their
   // meaning is per-mode, not shared.
   struct BrowserFilterState
   {
      char query[128] = "";
      int  sortMode = 0;
      int  typeFilter = 0;
      bool descending = false;
   };

extern std::string gAudioStartError;



   // ---- Offline Render (non-realtime export) state ----
   // One take at a time, across the whole patch (not per-OutputNode) - the
   // graph and AudioEngine are only ever driven by one clock at once, so two
   // Output nodes each trying to run their own offline session concurrently
   // would just race each other over the same Transport/AudioEngine calls.
   struct OfflineRenderState
   {
      bool active = false;         // a take is either rendering or finalizing
      OutputNode* node = nullptr;  // which OutputNode owns this take
      bool includeAudio = false;   // latched at start, mirrors node->includeAudio
      bool deviceWasRunning = false; // AudioEngine::Instance().SampleRate() > 0 before this take
      bool wasPlaying = true;        // Transport::Instance().IsPlaying() before this take
      bool vsyncWasOn = true;        // gVsync before this take; restored when it ends
      double startTime = 0.0;        // glfwGetTime() when the take was armed
      double startSeconds = 0.0;     // Transport::Instance().Seconds() when take was armed
      // Set by the pump whenever it yields because the encoder queue is full
      // rather than because it ran out of time budget. The two look identical
      // from outside - the frame counter stops either way - so the progress
      // window says which one is happening instead of leaving the user to
      // guess whether a render is slow or wedged.
      bool waitingOnEncoder = false;
      double lastProgressTime = 0.0; // when OfflineFramesDone last changed
      int lastFramesDone = -1;

      // Set only for a render started from the Arrangement Timeline's own
      // Render button - switches on two behaviors that would be wrong for
      // an ordinary manually-wired OutputNode take: (1) each frame, every
      // video lane's active clip is composited onto the node's FBO after
      // the cook (CompositeArrangeTimelineVideo - bottom lane first, so the
      // top lane is frontmost, each lane's blend mode and opacity applied),
      // replacing whatever the node's own Input produced; (2) RebuildAudioTopology's Timeline Strict terminals also
      // write into this node's capture ring (see arrangeAudioCapture
      // below), so the take's audio is the live sum of every active
      // timeline audio clip rather than whatever's cabled into AudioInput.
      bool arrangeDriven = false;
      // WP7 #6b splits arrangeDriven's two behaviours apart, because a job
      // can now take its picture from one place and its sound from another
      // ("video-only timeline over live canvas music" is a first-class job).
      // Both are only read while arrangeDriven.
      //   timelineVideo - composite the video lanes onto the take's FBO,
      //                   replacing whatever its own Input produced.
      //   timelineAudio - RebuildAudioTopology builds the Timeline Strict
      //                   terminals rather than the canvas Audio Outs
      //                   (ArrangeTimelineRoutingActive).
      // Audio always reaches the file through the take node's capture ring
      // either way: the timeline's export node has nothing cabled into its
      // AudioInput, so both sources are the master sum, and only the
      // terminals feeding that sum differ.
      bool timelineVideo = false;
      bool timelineAudio = false;
      // The job's range in ticks. StartOfflineRenderSession scopes its
      // hardware-source refusal (WP7 #4) to the clips that actually play
      // inside it, so it has to be set before the take is armed - same
      // discipline as arrangeDriven and endSeconds.
      int64_t rangeStartTick = 0;
      int64_t rangeEndTick = 0;
      // Only meaningful when arrangeDriven: the timeline second the render
      // should end on, so the render-finish cleanup can park the playhead
      // there instead of leaving it wherever Transport::SetOfflineMode(false)
      // happens to revert to (whatever mSeconds was before the take started).
      double endSeconds = 0.0;
   };

extern OfflineRenderState gOfflineRender;

extern Headless::Job gHeadlessJob;

extern int gHeadlessExitCode;

extern double gHeadlessAudioRate;

extern std::vector<Headless::Issue> gHeadlessPreWarnings;

extern bool gHeadlessNeedProbe;

extern std::vector<Headless::Issue> gHeadlessPromoted;

extern Patch::Data gHeadlessPatch;

extern std::map<std::string, int> gModulatableMax;

extern bool gHeadlessProbeAll;

extern std::set<int> gHeadlessDrawn;

bool HeadlessJobActive();

extern Patch::PerfLayoutRecord gPerfLayout;

extern std::vector<Patch::PerfRecord> gPerfElements;

extern Arrange::Model gArrange;

extern std::map<std::pair<int, int>, float> gPerfPendingWrites;

extern int   gCableVisibilityMask;

int DrumSequencerLaneForCanvasPos(DrumSequencerNode* n, float canvasX, float canvasY);



   // Persisted favourites for items across the browser panel modes:
   // modules (by name), samples (by file path), media (by file path),
   // plugins (by identifier), and field presets (by name). Saved to
   // AppPaths::AppSupportDir() + "/Infinite.browserfavorites".
   struct BrowserFavorites
   {
      std::unordered_set<std::string> modules;
      std::unordered_set<std::string> samples;
      std::unordered_set<std::string> media;
      std::unordered_set<std::string> plugins;
      std::unordered_set<std::string> fieldPresets;
      uint64_t version = 1;

      uint64_t Version() const { return version; }

      bool IsFavoriteModule(const std::string& name) const { return modules.count(name) > 0; }
      bool IsFavoriteSample(const std::string& path) const { return samples.count(path) > 0; }
      bool IsFavoriteMedia(const std::string& path) const { return media.count(path) > 0; }
      bool IsFavoritePlugin(const std::string& id) const { return plugins.count(id) > 0; }
      bool IsFavoriteFieldPreset(const std::string& name) const { return fieldPresets.count(name) > 0; }

      void ToggleModule(const std::string& name)
      {
         if (modules.count(name))
            modules.erase(name);
         else
            modules.insert(name);
         version++;
         Save();
      }

      void ToggleSample(const std::string& path)
      {
         if (samples.count(path))
            samples.erase(path);
         else
            samples.insert(path);
         version++;
         Save();
      }

      void ToggleMedia(const std::string& path)
      {
         if (media.count(path))
            media.erase(path);
         else
            media.insert(path);
         version++;
         Save();
      }

      void TogglePlugin(const std::string& id)
      {
         if (plugins.count(id))
            plugins.erase(id);
         else
            plugins.insert(id);
         version++;
         Save();
      }

      void ToggleFieldPreset(const std::string& name)
      {
         if (fieldPresets.count(name))
            fieldPresets.erase(name);
         else
            fieldPresets.insert(name);
         version++;
         Save();
      }

      std::string Path() const
      {
         const std::string dir = AppPaths::AppSupportDir();
         return dir.empty() ? std::string() : dir + "/Infinite.browserfavorites";
      }

      void Load()
      {
         const std::string path = Path();
         if (path.empty())
            return;
         std::ifstream file(path);
         if (!file)
            return;
         modules.clear();
         samples.clear();
         media.clear();
         plugins.clear();
         fieldPresets.clear();
         std::string line;
         std::string section;
         while (std::getline(file, line))
         {
            if (!line.empty() && line.back() == '\r')
               line.pop_back();
            if (line.empty() || line[0] == '#')
               continue;
            if (line.front() == '[' && line.back() == ']')
            {
               section = line.substr(1, line.size() - 2);
               continue;
            }
            if (section == "modules")
               modules.insert(line);
            else if (section == "samples")
               samples.insert(line);
            else if (section == "media")
               media.insert(line);
            else if (section == "plugins")
               plugins.insert(line);
            else if (section == "field")
               fieldPresets.insert(line);
         }
         version++;
      }

      void Save() const
      {
         const std::string path = Path();
         if (path.empty())
            return;
         std::ofstream file(path);
         if (!file)
            return;
         file << "[modules]\n";
         for (const auto& m : modules)
            file << m << "\n";
         file << "[samples]\n";
         for (const auto& s : samples)
            file << s << "\n";
         file << "[media]\n";
         for (const auto& m : media)
            file << m << "\n";
         file << "[plugins]\n";
         for (const auto& p : plugins)
            file << p << "\n";
         file << "[field]\n";
         for (const auto& f : fieldPresets)
            file << f << "\n";
      }
   };

extern BrowserFavorites gBrowserFavorites;

extern bool gPatchDirty;

extern std::set<std::pair<int, int>> gTypedParam;

extern std::map<std::pair<int, int>, std::string> gTypedParamText;

extern bool gParamRegisterOnly;

extern std::pair<int, int> gTypedParamJustOpened;

void BeginNodeParams(int nodeIndex);

void EndNodeParams();

extern std::set<ParamKey> gPredictorGrabs;

uint64_t UidForIndex(int nodeIndex);

float ParamToPos(const ParamRef& r, float v);

float PosToParam(const ParamRef& r, float pos);

void RegisterNodes();

IModulator* ModulatorForOutput(INode* node, int outputIndex);

GraphNode* FindNodeByIndex(int index);

GraphNode* FindNodeByUid(uint64_t uid);

int InputCountFor(const GraphNode& gn);

bool CanBypass(const GraphNode& gn);

ImageCable* CableFor(GraphNode& gn, int slot);



   // Upper bound on how many audio/note-input slots any single node exposes.
   // The widest today is Mixer, at MixerNode::kMaxSlots (12).
   inline const int kMaxAudioSlots = 12;

void RebuildAudioTopology();

void ForceAudioRepare();

void WireInputSlot(GraphNode& srcNode, GraphNode& dstNode, int slot, int srcOutputIndex = 0);



   // ---- ParamRef.key join results (see ParamKeyJoiner further down) ----
   struct ParamJoinType
   {
      std::map<int, std::string> keyOfParam; // paramIndex -> saved key
      std::map<std::string, int> paramOfKey;
      std::set<int> unkeyed;                 // registered controls with no key found
      std::map<int, std::string> unkeyedName; // their labels
      std::set<std::string> plainKeys;       // saved f/i/b keys no control registered
      std::map<std::string, std::vector<std::string>> optionsOfKey; // dropdown names by key
      std::vector<std::string> buttons;      // button labels the probe node drew, in draw order
      int registered = 0;
      bool done = false;
   };

extern std::map<std::string, ParamJoinType> gParamJoin;

extern std::map<int, std::vector<std::string>> gProbeButtons;

const PatchSchema::TypeSchema* SchemaFor(const std::string& typeName);

PatchSchema::Env MakeSchemaEnv(bool forRender);

GraphNode* SpawnNode(const std::string& typeName, const std::string& category,
                        float x = 0.0f, float y = 0.0f);



   // Field-declared params (`param float ...`) get explicit pin indices from
   // ParamTable's own id counter (starting at 1) so they stay stable across
   // recompiles even as native controls are added/removed above them in the
   // same node body. Offsetting them into their own sub-range keeps them from
   // colliding with gParamCounter-numbered native controls drawn earlier in
   // the same node (see PINDUPTEST / FieldPrimitiveNode's "count"+"max
   // elements" collision: two native int sliders claimed indices 0 and 1,
   // and the first declared param, with p.id == 1, collided with the second).
   inline const int kFieldDeclaredParamBase = 50;

void DrawFieldElementParams(FieldElementNode* n);

const std::vector<std::string>& MediaTypeFilterNames();

std::vector<const SampleScanner::Entry*> FilterAndSortSampleEntries(
      const std::vector<SampleScanner::Entry>& index, const std::string& lowerQuery,
      const BrowserFilterState& state, bool mediaKind);

bool ILess(const std::string& a, const std::string& b);

std::vector<const PluginScanner::Entry*> FilterAndSortPluginEntries(
      const std::vector<PluginScanner::Entry>& index, const std::string& lowerQuery, const BrowserFilterState& state);

extern bool gPatchDirty;

void ExportImage(INode* out, const std::string& path, int jpgQuality = 90,
                    std::vector<unsigned char>* keepPixels = nullptr, int outputIndex = 0);

bool ReadNodeImageRgba8(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<unsigned char>& rgba);

bool WriteSrgbPng(const std::string& path, int w, int h, const unsigned char* rgba);

bool NodeImageIsFloat(INode* node, int outputIndex);

bool ReadNodeImageRgba16(INode* node, int outputIndex, bool opaque, int& w, int& h, std::vector<uint16_t>& rgba);

std::vector<unsigned char> ResizeRgba8(const unsigned char* src, int w, int h, int nw, int nh);

std::vector<uint8_t> EncodePng16(int w, int h, const uint16_t* rgba, int level);



   // --frames-dir encoder: readback stays on the GL thread, zlib runs on a few workers behind a
   // byte-budgeted queue so a slow disk or a big frame back-pressures instead of eating memory.
   class PngSequenceWriter
   {
   public:
      explicit PngSequenceWriter(int level, size_t budgetBytes = 256u << 20) : mBudget(budgetBytes)
      {
         stbi_write_png_compression_level = level; // set once, before any worker reads it
         const unsigned n = std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2));
         for (unsigned i = 0; i < n; i++)
            mThreads.emplace_back([this] { Run(); });
      }
      ~PngSequenceWriter() { Finish(); }
      void Submit(std::string path, int w, int h, std::vector<unsigned char> rgba8, std::vector<uint16_t> rgba16, int level)
      {
         const size_t bytes = rgba8.size() + rgba16.size() * 2;
         std::unique_lock<std::mutex> lock(mMutex);
         mSpace.wait(lock, [&] { return mQueued == 0 || mQueued + bytes <= mBudget; });
         mQueued += bytes;
         mJobs.push_back({ std::move(path), w, h, std::move(rgba8), std::move(rgba16), level, bytes });
         mWork.notify_one();
      }
      // Waits for every queued frame; returns the paths that did not reach the disk.
      std::vector<std::string> Finish()
      {
         {
            std::lock_guard<std::mutex> lock(mMutex);
            mStop = true;
         }
         mWork.notify_all();
         for (std::thread& t : mThreads)
            if (t.joinable())
               t.join();
         mThreads.clear();
         return mFailed;
      }

   private:
      struct Job
      {
         std::string path;
         int w, h;
         std::vector<unsigned char> rgba8;
         std::vector<uint16_t> rgba16;
         int level;
         size_t bytes;
      };
      void Run()
      {
         for (;;)
         {
            Job job;
            {
               std::unique_lock<std::mutex> lock(mMutex);
               mWork.wait(lock, [&] { return mStop || !mJobs.empty(); });
               if (mJobs.empty())
                  return;
               job = std::move(mJobs.front());
               mJobs.pop_front();
            }
            bool ok = false;
            std::vector<uint8_t> png;
            if (!job.rgba16.empty())
               png = EncodePng16(job.w, job.h, job.rgba16.data(), job.level);
            else
            {
               stbi_write_png_to_func(
                  [](void* ctx, void* data, int size)
                  {
                     auto* bytes = static_cast<std::vector<uint8_t>*>(ctx);
                     bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
                  },
                  &png, job.w, job.h, 4, job.rgba8.data(), job.w * 4);
               if (!png.empty())
                  png = ContactSheet::TagSrgb(png);
            }
            if (!png.empty())
               if (FILE* f = std::fopen(job.path.c_str(), "wb"))
               {
                  ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
                  std::fclose(f);
               }
            std::lock_guard<std::mutex> lock(mMutex);
            if (!ok)
               mFailed.push_back(job.path);
            mQueued -= job.bytes;
            mSpace.notify_all();
         }
      }
      std::mutex mMutex;
      std::condition_variable mWork, mSpace;
      std::deque<Job> mJobs;
      std::vector<std::thread> mThreads;
      std::vector<std::string> mFailed;
      size_t mBudget = 0, mQueued = 0;
      bool mStop = false;
   };

float ApplyModulationCurve(float v, float curve);

void ArrangeSeekVideoSampleSources(double beat);

int OfflineAudioBlockFrames();

ImVec2 GetPerfElementCellSpan(int kind);

void ReorderPerfPages(int src, int dst);

void UpdatePerformanceMatrixMIDI();

bool ArrangeTimelineRoutingActive();

void ForceAudioRepare();

void RebuildAudioTopology();

bool StartAudioEngine(std::string& outError);

INode* FindHardwareDrivenNode();

void StartOfflineRenderSession(OutputNode* n, int width, int height, bool isArrange);

extern bool gPatchDirty;

extern bool gShowAutosaveRecoveryModal;

extern Patch::Data gPendingRecoveryData;

extern std::string gAutosaveRecoveryTimestamp;

extern std::string gAutosaveRecoveryError;

Patch::Data BuildPatchData();

std::string AutosavePath();

std::string AutosaveMarkerPath();

void CheckAutosaveRecovery();


   // deque, not vector: erase(begin()) at the depth cap below shifts every
   // remaining element, and each element is a full Patch::Data (two
   // heap-allocated strings per param per node) - expensive to shift at a
   // 200-deep cap on a large patch. pop_front() is O(1) on a deque.
   // One point in history. `patch` is the graph; `gestures` is the Shift-drag
   // recordings looping at that moment (see GestureRecorder). Recordings are
   // session state rather than patch content - they are not in Patch::Data
   // and never reach a saved file - but undo still has to make one appear and
   // disappear at the right point in history, exactly the way a typed
   // expression (which IS in Patch::Data) already does. Snapshotting them
   // alongside the graph is what gives that: the checkpoint pushed when the
   // user grabbed the knob predates the recording, so undoing to it removes
   // the recording, and redo brings it back. Carried across ApplyPatchData's
   // respawn through the same old-index -> new-index remap as RemapGestures.
   struct UndoEntry
   {
      Patch::Data patch;
      GestureRecorder::PlaybackMap gestures;
      // Set only for timeline-only gestures (move a clip, trim, split, add a
      // marker). Undoing one of these swaps the arrangement back and touches
      // nothing else - it must NOT go through ApplyPatchData, which tears down
      // and respawns the whole graph. That respawn is why dragging a clip used
      // to reset every node's internal state, drop audio, and rebuild every
      // FBO. Clips reference node uids, which the graph side never changes
      // here, so the two entry kinds coexist with no remapping.
      bool arrangeOnly = false;
      Arrange::Model arrange;
   };

extern std::deque<UndoEntry> gUndoStack;

extern std::deque<UndoEntry> gRedoStack;

double GesturePlaybackClock();

void GestureSyncClockAxis();

void NewPatch();

void ApplyPatchData(const Patch::Data& data, std::map<int, int>* outRemap = nullptr, bool keepIndices = false);

bool LoadPatchFromImpl(const std::string& path, bool reload);

bool LoadPatchFrom(const std::string& path);

void FinishCanonicalize(Patch::Data& data, const Headless::Job& job, Headless::Status& st);

void PushUndoCheckpoint();

void Undo();

void Redo();

bool HandleRpcCommand(const std::string& method, const nlohmann::json& params,
                         nlohmann::json& outResult, std::string& outError);

std::string BundledResourcePath(const char* relPath);

extern bool gPatchChangedOnDisk;

void PollPatchFileWatch(bool force = false);


// ================================================== Audio node sweep discovery
//
// Shared by INFINITE_AUDIOPARAMSWEEPTEST and INFINITE_AUDIOTEARDOWNSWEEPTEST:
// every node type is read out of NodeFactory - the same registry
// RegisterNodes() populates - rather than a hand-maintained list, so a node
// added later (any remaining row of docs/plans/audio/README.md §3) is
// covered by both sweeps without anyone editing this file. Which of a node's
// interfaces it answers (IAudioSource, INoteSource, AudioNodeForNotePorts(),
// AudioInputSlot/NoteInputSlot) is probed the same way InputCountFor already
// probes AudioInputSlot/NoteInputSlot generically for pin counts - see
// .claude/skills/new-audio-node/SKILL.md §3's step 9 note that audio and note
// pins share one slot index space.
struct AudioNodeShape
{
   bool isAudioSource = false;
   bool isNoteSource = false;
   bool isModulator = false;
   bool hasNotePorts = false; // AudioNodeForNotePorts() != nullptr (NoteToCVNode)
   int audioInputSlots = 0;   // highest AudioInputSlot(i) != nullptr index + 1
   int noteInputSlots = 0;    // highest NoteInputSlot(i) != nullptr index + 1
   int firstNoteInputSlot = -1; // actual slot index of the first live NoteInputSlot(); -1 if none.
                                 // Nodes with note-only input (no AudioInputSlot at all) can have
                                 // their note pin start at a nonzero slot - e.g. WaveTerrainNode and
                                 // ImageSpectralSynthNode both only answer NoteInputSlot(1). Callers
                                 // that need to actually connect a cable must use this, not slot 0.

   // Does this node own (or reach) an AudioNode at all? AUDIOPARAMSWEEPTEST's
   // "reaches the audio thread" check needs one to drive; a node with none
   // (Audio Out - a pure topology terminal, no ProcessBlock of its own) has
   // nothing to test there.
   bool HasAudioNode() const { return isAudioSource || isNoteSource || hasNotePorts; }

   // Does this node touch the audio/note cable graph at all, as either side
   // of a cable? AUDIOTEARDOWNSWEEPTEST's teardown invariant applies to every
   // one of these, including pure terminals like Audio Out.
   bool ParticipatesInAudioGraph() const
   {
      return isAudioSource || isNoteSource || hasNotePorts || audioInputSlots > 0 || noteInputSlots > 0;
   }
};



struct AudioSweepCandidate
{
   std::string name;
   std::string category;
   AudioNodeShape shape;
};

std::vector<AudioSweepCandidate> DiscoverAudioSweepCandidates(
   const std::function<bool(const AudioNodeShape&)>& include);

bool RunGainFixture();

bool RunFilterFixture();

bool RunOscWaveformFixture();

bool RunWavetableFixture();

bool RunEnvelopeFixture();

bool RunVoiceStealFixture();

bool RunMusicTimeFixture();

bool RunAudioFilterFixture();

bool RunDynamicsFixture();



// Delay's exit criterion, same rig shape as DynamicsTest above (real
// AudioEffectNode -> AudioEffectRuntime -> DelayKernel pipeline, not the
// kernel's internals in isolation).
namespace DelayTest
{
   inline std::unique_ptr<AudioEffectNode> MakeDelayNode(bool bounce, double sampleRate)
   {
      const EffectDef* def = nullptr;
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Delay")
            def = &d;
      auto node = std::make_unique<AudioEffectNode>(*def);
      *node->ParamPtr("bounce") = bounce ? 1.0f : 0.0f;
      node->mix = 1.0f; // 100% wet so the fixture measures the delayed tap directly, not a dry/wet blend
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   // Runs `numSamples` frames of `numChannels`, one sample at a time (block
   // size 1) through the node, capturing every channel's output.
   template <typename SampleFn>
   void RunSamples(AudioEffectNode& node, int numSamples, int numChannels, SampleFn getSample,
                    std::vector<std::vector<float>>* outSamples)
   {
      AudioNode* audioNode = node.GetAudioNode();
      float inVal[2] = { 0.0f, 0.0f };
      float outVal[2] = { 0.0f, 0.0f };
      float* inPtrs[2] = { &inVal[0], &inVal[1] };
      float* outPtrs[2] = { &outVal[0], &outVal[1] };
      AudioBuffer inBuffer;
      inBuffer.channels = inPtrs;
      inBuffer.numChannels = numChannels;
      inBuffer.numFrames = 1;
      AudioBuffer outBuffer;
      outBuffer.channels = outPtrs;
      outBuffer.numChannels = numChannels;
      outBuffer.numFrames = 1;
      const AudioBuffer* inputs[1] = { &inBuffer };
      if (outSamples != nullptr)
         outSamples->assign(numChannels, {});
      for (int i = 0; i < numSamples; i++)
      {
         for (int ch = 0; ch < numChannels; ch++)
            inVal[ch] = getSample(i, ch);
         audioNode->ProcessBlock(inputs, 1, outBuffer);
         if (outSamples != nullptr)
            for (int ch = 0; ch < numChannels; ch++)
               (*outSamples)[ch].push_back(outVal[ch]);
      }
   }
}

bool RunDelayFixture();

bool RunReverbFixture();

bool RunWavetableShaperFixture();

bool RunSamplerFixture();

bool RunPaulStretchFixture();

bool RunResonatorFixture();

bool RunMetallicDecayFixture();

bool RunCycleShaperFixture();

bool RunMidiFileFixture();

bool RunMpeFixture();

bool RunShapeResonatorFixture();

bool RunSpectrumSlideFixture();

bool RunKeySnapFixture();

bool RunSpecBlurFixture();

bool RunGrainMolderFixture();

bool RunMolderFixture();

bool RunSpoutLoopTest();

bool RunGranularFixture();

int RunDspTest();

int RunFieldTest();

int RunFieldElementTest();

int RunFieldParamTest();

int RunFieldStateTest();

int RunFieldTransferTest();

int RunFieldSampleTest();

int RunFieldPinDeclTest();

void RunRecSyncTest();

void RunAudioRingTest();

void RunVideoExactTest();

void RunRecExportTest(int width, int height, bool starved, const char* label);

bool RunAudioPdcTest();

#if defined(__linux__)
int RunMidiParseTest();
#endif

int RunCVRecorderTest();

int RunMidiCC14Test();

int RunAudioParamSweepTest();

bool RunFmModeDebugCheck();

bool RunFmRenderCheck();

bool RunBrowserSortTest();

bool RunAppearanceSelfTest();

int RunPluginScanTest();

int RunMidiBendTest();

int RunPluginNodeHandleTest();

#if INFINITE_ENABLE_VST3
int RunVST3ScanTest();
#endif

#if INFINITE_ENABLE_VST3
#if defined(__linux__)
int RunVST3EditorShotTest();
#endif
#endif

#if INFINITE_ENABLE_VST3
#if defined(__linux__)
int RunVST3BlocklistTest();
#endif
#endif

void RunRpcBatchTest();

void RunPatchWatchTest();

int RunAutosaveMarkerTest();

int RunRemoveBgTest();

int RunNetworkTest();

float ShapeToParam(const ParamRef& ref, float v);

bool RunPerfPanelSelfTest();

void ArrangeRefreshActiveClipModBypass();

bool ArrangeClipBypassesMod(int nodeIndex, int paramIndex);

float ArrangeClipBypassBaseValue(const ParamRef& ref, const Modulation::Source& src);

void RefreshParamDriverFlags();

void ApplyModulationAndPalette(int frameId, bool isNormalFrame = false);

#if defined(__linux__)
int RunCameraConvTest();
#endif

#if defined(__linux__)
int RunHostEnvTest();
#endif

int RunSyphonPatchTest();

int RunPatchLayoutTest();

void BenchFillXruns(Bench::BenchReport& report, const AudioEngine::XrunCounts& base);

void BuildBenchB1Audio(long numVoices, int bufferFrames = 256, float yOffset = 0.0f);

void BuildBenchB2Scene(const std::string& scaleStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx,
                              int* outTwistIdx = nullptr, int* outMatIdx = nullptr, int* outCamIdx = nullptr,
                              bool useEmbossForGlitch = false);

void BuildBenchB4Scene(const std::string& scaleStr, const std::string& shadowStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx, int& outCamIdx, int& outLfoIdx);

void BuildBenchB6Scene(int n, bool collapsed, float& outMaxX, float& outMaxY, int& outDragNodeIdx);

bool BuildBenchB8Scene(const std::vector<std::string>& clipPaths, bool withSyphon, bool withCamera, int windows,
                              std::vector<int>& outClipIdx, std::vector<int>& outOutputIdx,
                              int& outSyphonIdx, int& outCameraIdx, std::string& outError);

std::string ExplainLive(bool json, bool all);

void JoinLiveTier1();

void HeadlessTick(int& frameId, GLFWwindow* window);
}
