#include "AudioPluginNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio/AudioEngine.h"
#include "audio/AudioNode.h"

// ---------------------------------------------------------------- audio half
//
// Deliberately tiny. Read the atomic handle once, then either pass audio
// through (nothing loaded, or bypassed - a plugin node that hasn't finished
// loading must not mute the chain it sits in) or call the plugin's cached
// render block. That is the entire audio-thread surface of this feature:
// no Objective-C, no ARC, no allocation, no lock, no mailbox.
class AudioPluginAudioNode : public AudioNode
{
public:
   void PrepareToPlay(double sampleRate, int maxBlockSize) override
   {
      // Recorded, not acted on. The plugin's own allocateRenderResources is
      // main-thread work (and can block), so AudioPluginNode::CookIfNeeded
      // does it - reading AudioEngine's rate directly - and only publishes the
      // handle once it has been prepared at that rate.
      mSampleRate.store(sampleRate, std::memory_order_relaxed);
      mMaxBlockSize.store(maxBlockSize, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const AudioBuffer* in = (numInputs > 0) ? inputs[0] : nullptr;
      Platform::PluginHandle* handle = mHandle.load(std::memory_order_acquire);

      if (handle == nullptr || mBypass.load(std::memory_order_relaxed))
      {
         PassThrough(in, output);
         return;
      }

      // Notes go through the note queue, not ParamMailbox - see the class
      // comment's mapped-params exception, which is unrelated to this. Drained
      // before the render call so every event lands in the same block its
      // frameOffset was scheduled against.
      if (mNoteInbox != nullptr)
      {
         NoteEvent evts[64];
         const int numEvts = mNoteInbox->Pop(evts, 64);
         for (int i = 0; i < numEvts; i++)
         {
            const unsigned char status = (unsigned char)((evts[i].isNoteOn ? 0x90 : 0x80));
            const unsigned char note = (unsigned char)std::clamp(evts[i].note, 0, 127);
            const unsigned char velocity =
               (unsigned char)std::clamp((int)std::lround(evts[i].velocity * 127.0f), 0, 127);
            const unsigned char bytes[3] = { status, note, velocity };
            Platform::PluginScheduleMIDIEvent(handle, evts[i].frameOffset, bytes, 3);
         }
      }

      const float* inChannels[kAudioMaxChannels] = {};
      int inCount = 0;
      if (in != nullptr && in->channels != nullptr && in->numFrames > 0)
      {
         inCount = std::min(in->numChannels, (int)kAudioMaxChannels);
         for (int ch = 0; ch < inCount; ch++)
            inChannels[ch] = in->channels[ch];
      }

      Platform::PluginRender(handle, inCount > 0 ? inChannels : nullptr, inCount, output.channels,
                             output.numChannels, output.numFrames);
   }

   // Deliberately no Reset() override: AudioNode::Reset means "clear DSP
   // state", and dropping the published plugin handle there would silently
   // unload the plugin. The plugin's own state is the plugin's business.

   // A note-consuming node's inbox, wired by RebuildAudioTopology's note pass
   // (main.cpp) - see AudioNode::SetNoteInbox's comment. nullptr (the
   // default) when the note pin is unconnected or this plugin doesn't accept
   // notes at all; ProcessBlock just skips the drain in that case.
   void SetNoteInbox(NoteEventQueue* inbox) override { mNoteInbox = inbox; }

   // Main thread only.
   void SetHandle(Platform::PluginHandle* handle) { mHandle.store(handle, std::memory_order_release); }
   void SetBypass(bool bypass) { mBypass.store(bypass, std::memory_order_relaxed); }
   double SampleRate() const { return mSampleRate.load(std::memory_order_relaxed); }
   int MaxBlockSize() const { return mMaxBlockSize.load(std::memory_order_relaxed); }

private:
   static void PassThrough(const AudioBuffer* in, AudioBuffer& output)
   {
      for (int ch = 0; ch < output.numChannels; ch++)
      {
         float* dst = output.channels[ch];
         if (dst == nullptr)
            continue;
         const float* src = nullptr;
         if (in != nullptr && in->channels != nullptr && in->numChannels > 0)
            src = in->channels[ch < in->numChannels ? ch : in->numChannels - 1];
         if (src != nullptr)
         {
            const int frames = std::min(output.numFrames, in->numFrames);
            for (int i = 0; i < frames; i++)
               dst[i] = src[i];
            for (int i = frames; i < output.numFrames; i++)
               dst[i] = 0.0f;
         }
         else
         {
            for (int i = 0; i < output.numFrames; i++)
               dst[i] = 0.0f;
         }
      }
   }

   std::atomic<Platform::PluginHandle*> mHandle { nullptr };
   std::atomic<bool> mBypass { false };
   NoteEventQueue* mNoteInbox = nullptr; // set by SetNoteInbox; see its comment
   std::atomic<double> mSampleRate { 0.0 };
   std::atomic<int> mMaxBlockSize { 0 };
};

// ----------------------------------------------------------------- main half

AudioPluginNode::AudioPluginNode() = default;

AudioPluginNode::~AudioPluginNode()
{
   // Unpublish first, then tear down. Platform::PluginDestroy waits out an
   // in-flight render before touching the unit, so the window between the
   // store below and the destroy is closed on that side rather than here.
   if (mAudioNode)
      mAudioNode->SetHandle(nullptr);
   DestroyAllHandles();
}

AudioNode* AudioPluginNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPluginAudioNode>();
   return mAudioNode.get();
}

void AudioPluginNode::DestroyAllHandles()
{
   // mHandle is either the same object as mLive (published and ready) or a
   // still-loading instance that was never published, so it is compared
   // against the *pre-clear* values - clearing mLive first and then testing
   // mHandle != mLive would double-destroy the common case.
   Platform::PluginHandle* live = mLive;
   Platform::PluginHandle* retired = mRetired;
   Platform::PluginHandle* pending = mHandle;
   mLive = nullptr;
   mRetired = nullptr;
   mHandle = nullptr;

   if (retired != nullptr && retired != live)
      Platform::PluginDestroy(retired);
   if (live != nullptr)
      Platform::PluginDestroy(live);
   if (pending != nullptr && pending != live && pending != retired)
      Platform::PluginDestroy(pending);
}

void AudioPluginNode::PublishHandle(Platform::PluginHandle* handle)
{
   // One-generation retire, exactly as AudioEngine::SetTopology does it: the
   // generation before last is provably unreachable from any callback that
   // could still be running, so it is safe to destroy here; the one being
   // displaced is only retired.
   if (mRetired != nullptr)
   {
      Platform::PluginDestroy(mRetired);
      mRetired = nullptr;
   }
   mRetired = mLive;
   mLive = handle;
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPluginAudioNode>();
   mAudioNode->SetHandle(handle);
}

void AudioPluginNode::LoadPlugin(const Platform::PluginDesc& desc)
{
   // A different plugin's parameter addresses mean nothing to this one, so the
   // whole mapping list goes with it rather than silently re-pointing at
   // whatever happens to share an address.
   for (int i = 0; i < kMaxMappedParams; i++)
   {
      mappings[i] = Mapping();
      mLastPushedValid[i] = false;
   }
   mAvailableParams.clear();
   mPluginState.clear();
   mPendingStateRestore = false;
   SetConfiguring(false);

   pluginFormat = desc.format;
   pluginId = desc.identifier;
   pluginName = desc.name;
   mAcceptsNotes = desc.acceptsNotes;
   if (!mAcceptsNotes)
      noteInput.Disconnect();

   // Unpublish before creating: the previous plugin must stop being reachable
   // from the audio thread the moment the user asks for a different one, not
   // whenever the new one finishes loading.
   if (mAudioNode)
      mAudioNode->SetHandle(nullptr);
   if (mRetired != nullptr)
   {
      Platform::PluginDestroy(mRetired);
      mRetired = nullptr;
   }
   mRetired = mLive;
   mLive = nullptr;
   if (mHandle != nullptr && mHandle != mRetired)
      Platform::PluginDestroy(mHandle);

   mReady = false;
   mLoadFailed = false;
   mPreparedRate = 0.0;
   mPreparedBlock = 0;
   mStatus = "loading " + desc.name + "...";

   const double rate = AudioEngine::Instance().SampleRate();
   mHandle = Platform::PluginCreate(desc, rate, kAudioMaxBlockFrames);
}

void AudioPluginNode::ReloadFromIdentity()
{
   if (pluginId.empty())
      return;

   Platform::PluginDesc desc;
   desc.format = pluginFormat.empty() ? std::string("au") : pluginFormat;
   desc.identifier = pluginId;
   desc.name = pluginName;
   // Restored from the just-loaded VisitParams (see its v.Bool call) rather
   // than re-derived - LoadPlugin below assigns mAcceptsNotes straight from
   // this, so leaving it default-false here would silently drop the note pin
   // off a reloaded instrument/music-effect patch.
   desc.acceptsNotes = mAcceptsNotes;

   // Keep the restored mapping list and fullState - unlike LoadPlugin, this is
   // the *same* plugin coming back, so its addresses still mean what they did.
   const std::string savedState = mPluginState;
   Mapping saved[kMaxMappedParams];
   for (int i = 0; i < kMaxMappedParams; i++)
      saved[i] = mappings[i];

   LoadPlugin(desc);

   for (int i = 0; i < kMaxMappedParams; i++)
      mappings[i] = saved[i];
   mPluginState = savedState;
   mPendingStateRestore = !savedState.empty();
}

void AudioPluginNode::Unload()
{
   if (mAudioNode)
      mAudioNode->SetHandle(nullptr);
   SetConfiguring(false);
   DestroyAllHandles();
   pluginFormat.clear();
   pluginId.clear();
   pluginName.clear();
   mAcceptsNotes = false;
   noteInput.Disconnect();
   mPluginState.clear();
   mPendingStateRestore = false;
   mAvailableParams.clear();
   mReady = false;
   mLoadFailed = false;
   mStatus = "no plugin";
   for (int i = 0; i < kMaxMappedParams; i++)
   {
      mappings[i] = Mapping();
      mLastPushedValid[i] = false;
   }
}

void AudioPluginNode::ToggleEditor()
{
   if (mHandle == nullptr || !mReady)
      return;
   if (Platform::PluginEditorIsOpen(mHandle))
   {
      Platform::PluginCloseEditor(mHandle);
      return;
   }
   std::string error;
   if (!Platform::PluginOpenEditor(mHandle, error))
      mStatus = "editor: " + error;
}

bool AudioPluginNode::EditorIsOpen() const
{
   return mHandle != nullptr && Platform::PluginEditorIsOpen(mHandle);
}

void AudioPluginNode::SetConfiguring(bool on)
{
   if (mConfiguring == on)
      return;
   mConfiguring = on;
   if (mHandle == nullptr)
      return;
   if (on)
      Platform::PluginBeginLearn(mHandle);
   else
      Platform::PluginEndLearn(mHandle);
}

void AudioPluginNode::RefreshAvailableParams()
{
   mAvailableParams.clear();
   if (mHandle == nullptr || !mReady)
      return;
   const int count = Platform::PluginParameterCount(mHandle);
   mAvailableParams.reserve((size_t)count);
   for (int i = 0; i < count; i++)
   {
      Platform::PluginParamInfo info;
      if (Platform::PluginParameterInfo(mHandle, i, info))
         mAvailableParams.push_back(std::move(info));
   }
}

int AudioPluginNode::MapParameter(unsigned long long address)
{
   if (mHandle == nullptr || !mReady)
      return -1;

   for (int i = 0; i < kMaxMappedParams; i++)
      if (mappings[i].assigned && mappings[i].address == address)
         return i; // already mapped - touching it again isn't a second slot

   Platform::PluginParamInfo info;
   if (!Platform::PluginParameterInfoByAddress(mHandle, address, info))
      return -1;

   for (int i = 0; i < kMaxMappedParams; i++)
   {
      if (mappings[i].assigned)
         continue;
      mappings[i].assigned = true;
      mappings[i].address = info.address;
      mappings[i].displayName = info.displayName;
      mappings[i].minValue = info.minValue;
      mappings[i].maxValue = info.maxValue;
      float current = info.defaultValue;
      Platform::PluginGetParameter(mHandle, address, current);
      mappings[i].value = std::clamp(current, info.minValue, info.maxValue);
      mLastPushed[i] = mappings[i].value;
      mLastPushedValid[i] = true;
      return i;
   }
   return -1;
}

void AudioPluginNode::UnmapSlot(int slot)
{
   if (slot < 0 || slot >= kMaxMappedParams)
      return;
   // Cleared in place, never erased: see Mapping's comment on why slots are
   // positional.
   mappings[slot] = Mapping();
   mLastPushedValid[slot] = false;
}

int AudioPluginNode::HighestAssignedSlot() const
{
   for (int i = kMaxMappedParams - 1; i >= 0; i--)
      if (mappings[i].assigned)
         return i;
   return -1;
}

int AudioPluginNode::AssignedCount() const
{
   int n = 0;
   for (int i = 0; i < kMaxMappedParams; i++)
      if (mappings[i].assigned)
         n++;
   return n;
}

void AudioPluginNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   mCookCounter++;

   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioPluginAudioNode>();
   mAudioNode->SetBypass(bypass);

   if (mHandle == nullptr)
      return;

   // 1. Finish an in-flight instantiation. Polling is what keeps the app
   //    responsive while a slow out-of-process AUv3 comes up.
   if (!mReady && !mLoadFailed)
   {
      std::string error;
      const Platform::PluginLoadState state = Platform::PluginPoll(mHandle, error);
      if (state == Platform::PluginLoadState::Pending)
         return;
      if (state == Platform::PluginLoadState::Failed)
      {
         mLoadFailed = true;
         mStatus = error.empty() ? std::string("failed to load") : error;
         return;
      }
      mReady = true;
      mStatus = pluginName;
      RefreshAvailableParams();
      if (mConfiguring)
         Platform::PluginBeginLearn(mHandle);
   }

   if (mLoadFailed)
      return;

   // 2. Prepare at the device's real rate, and re-prepare if it changed
   //    (device swap, sleep/wake). Unpublish across the re-prepare: the
   //    plugin's render resources are torn down and rebuilt inside it.
   // The engine's rate when there is a device, otherwise whatever the topology
   // builder handed the audio half in PrepareToPlay. Both are the same number
   // in the running app; taking either means the node still prepares and
   // publishes under a headless harness that drives PrepareToPlay directly
   // without opening a device (INFINITE_PLUGINSCANTEST does exactly that).
   double rate = AudioEngine::Instance().SampleRate();
   if (rate <= 0.0)
      rate = mAudioNode->SampleRate();
   if (rate > 0.0 && (rate != mPreparedRate || mPreparedBlock != kAudioMaxBlockFrames))
   {
      mAudioNode->SetHandle(nullptr);
      std::string error;
      if (!Platform::PluginPrepare(mHandle, rate, kAudioMaxBlockFrames, error))
      {
         mLoadFailed = true;
         mStatus = error.empty() ? std::string("prepare failed") : error;
         return;
      }
      mPreparedRate = rate;
      mPreparedBlock = kAudioMaxBlockFrames;

      // 3. A restored patch's plugin state is applied here, once, after the
      //    plugin is fully prepared - fullState invalidates render resources,
      //    so applying it earlier would be undone by the prepare above.
      if (mPendingStateRestore)
      {
         Platform::PluginRestoreState(mHandle, mPluginState);
         mPendingStateRestore = false;
         RefreshAvailableParams();
         for (int i = 0; i < kMaxMappedParams; i++)
            mLastPushedValid[i] = false; // force a push of every saved value
      }

      PublishHandle(mHandle);
   }

   // 4. Learn: an address the plugin's own editor reported while configure is
   //    on. The observer stored it from an arbitrary thread; this is the only
   //    place it is read.
   if (mConfiguring)
   {
      unsigned long long learned = 0;
      if (Platform::PluginPollLearned(mHandle, learned))
         MapParameter(learned);
   }

   // 5. Push mapped values the user (or a modulator, or an expression) moved
   //    this frame. Straight to the plugin, not through ParamMailbox - see the
   //    class comment.
   for (int i = 0; i < kMaxMappedParams; i++)
   {
      if (!mappings[i].assigned)
         continue;
      const float v = std::clamp(mappings[i].value, mappings[i].minValue, mappings[i].maxValue);
      if (mLastPushedValid[i] && v == mLastPushed[i])
         continue;
      Platform::PluginSetParameter(mHandle, mappings[i].address, v);
      mLastPushed[i] = v;
      mLastPushedValid[i] = true;
   }

   // 6. Read back, so turning a knob in the plugin's own window moves this
   //    node's slider. Rate-limited to two slots per frame rather than all 32:
   //    CookIfNeeded's budget is microseconds, and a mapped control catching up
   //    within a few frames is imperceptible. Gated on the editor actually
   //    being open: with it closed nothing can change the plugin's values
   //    behind the node's back (the read-back exists solely to catch the user
   //    turning a knob in the plugin's own window), so the poll - an XPC round
   //    trip per call for an out-of-process AUv3 - is pure cost otherwise.
   if ((mCookCounter & 1) == 0 && Platform::PluginEditorIsOpen(mHandle))
   {
      for (int probe = 0; probe < 2; probe++)
      {
         mReadbackCursor = (mReadbackCursor + 1) % kMaxMappedParams;
         const int i = mReadbackCursor;
         if (!mappings[i].assigned)
            continue;
         float current = mappings[i].value;
         if (!Platform::PluginGetParameter(mHandle, mappings[i].address, current))
            continue;
         // Only adopt the plugin's value when it disagrees with what we last
         // pushed - otherwise a slider the user is mid-drag on would fight the
         // read-back for a frame every time.
         if (mLastPushedValid[i] && current != mLastPushed[i])
         {
            mappings[i].value = std::clamp(current, mappings[i].minValue, mappings[i].maxValue);
            mLastPushed[i] = mappings[i].value;
         }
      }
   }
}

void AudioPluginNode::VisitParams(ParamVisitor& v)
{
   // Refreshing the plugin's own state here, inside VisitParams, is what makes
   // it save-current rather than save-stale. The direction of the visit isn't
   // observable, but it doesn't need to be: on a save the handle is live and
   // this captures the plugin's real state; on a load (or paste into a fresh
   // node) there is no handle yet and this is a no-op, leaving mPluginState
   // free to be filled in by v.Text below.
   if (mHandle != nullptr && mReady)
      Platform::PluginSaveState(mHandle, mPluginState);

   v.Text("plugin_format", pluginFormat);
   v.Text("plugin_id", pluginId);
   v.Text("plugin_name", pluginName);
   v.Bool("plugin_accepts_notes", mAcceptsNotes);
   v.Text("plugin_state", mPluginState);
   v.Bool("bypass", bypass);

   // Only the slots up to the highest assigned one are written, with the count
   // first so the load side knows how many to read back. Trailing empty slots
   // carry no information and 32 x 6 unconditional keys per node would bloat
   // every patch that has a plugin in it.
   int slotCount = HighestAssignedSlot() + 1;
   v.Int("map_slots", slotCount);
   slotCount = std::clamp(slotCount, 0, kMaxMappedParams);

   char name[32];
   for (int i = 0; i < slotCount; i++)
   {
      snprintf(name, sizeof(name), "map%d_assigned", i);
      v.Bool(name, mappings[i].assigned);
      // The address is a 64-bit AUParameterAddress and ParamVisitor has no
      // 64-bit type (INode.h), so it round-trips as a decimal string rather
      // than being silently narrowed through Int or Float.
      snprintf(name, sizeof(name), "map%d_addr", i);
      std::string addr = std::to_string(mappings[i].address);
      v.Text(name, addr);
      mappings[i].address = strtoull(addr.c_str(), nullptr, 10);
      snprintf(name, sizeof(name), "map%d_name", i);
      v.Text(name, mappings[i].displayName);
      snprintf(name, sizeof(name), "map%d_min", i);
      v.Float(name, mappings[i].minValue);
      snprintf(name, sizeof(name), "map%d_max", i);
      v.Float(name, mappings[i].maxValue);
      snprintf(name, sizeof(name), "map%d_value", i);
      v.Float(name, mappings[i].value);
   }
}
