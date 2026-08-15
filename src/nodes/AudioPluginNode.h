#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"
#include "platform/Platform.h"

class AudioPluginAudioNode;

// Hosts a third-party audio plugin (Audio Units today) as an ordinary audio
// node: audio in at slot 0, audio out, an optional note-input pin at slot 1
// (only for a plugin that is an instrument or music effect - see
// AcceptsNotes), the plugin's own editor in a separate native window, and an
// Ableton-style "configure" list of mapped plugin parameters drawn as
// modulatable horizontal sliders on the node body.
//
// The two-object rule applies as usual: this half runs on the main thread and
// owns AudioPluginAudioNode, which runs on the audio thread. What is unusual
// is the third object - the plugin instance itself, an opaque
// Platform::PluginHandle. It is created, prepared, parameterised and destroyed
// entirely from this main-thread half; the audio half only ever reads a
// std::atomic<PluginHandle*> and calls Platform::PluginRender through it.
//
// Two things follow from that, and both are load-bearing:
//
//  - Mapped plugin parameters do NOT go through ParamMailbox. They are pushed
//    straight to the plugin with Platform::PluginSetParameter from
//    CookIfNeeded, because the plugin owns its own parameter smoothing and a
//    mailbox slot per mapped param would both duplicate that and blow past
//    ParamMailbox's 64-slot ceiling. This is the one deliberate exception to
//    the "every VisitParams param reaches the audio thread through the
//    mailbox" rule, and it is why this node carries a documented
//    AUDIOPARAMSWEEPTEST baseline in the hygiene driver.
//
//  - Swapping or unloading a plugin retires the old handle for one generation
//    rather than destroying it immediately, mirroring
//    AudioEngine::SetTopology's discipline: an in-flight render callback may
//    still be inside the handle at the moment the atomic is overwritten.
class AudioPluginNode : public INode, public IAudioSource
{
public:
   // Positional mapping slots. 32 is well past the point a node body stops
   // being readable, and ParamMailbox's 64-slot ceiling is the other bound
   // in the neighbourhood - the real limit is the eye, not either number.
   static constexpr int kMaxMappedParams = 32;

   // One mapped plugin parameter. Slots are POSITIONAL and are never
   // compacted: modulation bindings are keyed by (nodeIndex, paramIndex) and
   // paramIndex is this node body's ModSlider call order, so removing a middle
   // row would silently re-point every later row's modulation pin at a
   // different parameter. Unmapping clears `assigned` and leaves the row where
   // it is.
   struct Mapping
   {
      bool assigned = false;
      unsigned long long address = 0;
      std::string displayName;
      float minValue = 0.0f;
      float maxValue = 1.0f;
      float value = 0.0f;
   };

   static INode* Create() { return new AudioPluginNode(); }
   AudioPluginNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~AudioPluginNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   // Slot 1, not a second audio slot - notes and audio share one pin-index
   // space (see main.cpp's pin-count loop), so the note pin lands one past
   // the audio input regardless of which the loaded plugin actually accepts.
   // Only shown/wireable once a plugin that accepts notes is loaded (an
   // instrument or music-effect AU) - see DrawPluginBody / the pin-count loop.
   NoteCable* NoteInputSlot(int slot) override { return slot == 1 && AcceptsNotes() ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      if (slot == 0)
         return "in";
      if (slot == 1 && AcceptsNotes())
         return "notes";
      return nullptr;
   }

   // True once the loaded plugin is an instrument or music effect - what the
   // note pin's visibility is keyed off. False with no plugin loaded.
   bool AcceptsNotes() const { return mAcceptsNotes; }

   // Starts loading `desc`. Asynchronous - the node reports "loading..." until
   // CookIfNeeded sees the instantiation finish. Clears any existing mapping
   // list, since a different plugin's parameter addresses mean nothing to it.
   void LoadPlugin(const Platform::PluginDesc& desc);

   // Post-load / post-paste hook (main.cpp's ReloadDerivedState): re-creates
   // the plugin from the identity VisitParams just restored, and queues the
   // saved fullState to be applied once it is ready.
   void ReloadFromIdentity();

   void Unload();

   const std::string& PluginDisplayName() const { return pluginName; }
   const std::string& Status() const { return mStatus; }
   bool IsReady() const { return mReady; }
   bool IsLoading() const { return mHandle != nullptr && !mReady && mLoadFailed == false; }
   bool HasPlugin() const { return !pluginId.empty(); }

   // The plugin's own editor window. Toggling is the node's "open" button;
   // EditorIsOpen reads the real window state, so a window the user closed
   // with the red button reports closed here on the next frame.
   void ToggleEditor();
   bool EditorIsOpen() const;

   void SetConfiguring(bool on);
   bool Configuring() const { return mConfiguring; }

   // Fills the first unassigned slot with `address` (or returns the slot that
   // already holds it). Returns -1 if every slot is taken or the address isn't
   // a parameter of the loaded plugin.
   int MapParameter(unsigned long long address);
   void UnmapSlot(int slot);

   // Highest slot index with assigned == true, or -1. Only rows past this are
   // hidden; middle unassigned rows stay drawn (see Mapping's comment).
   int HighestAssignedSlot() const;
   int AssignedCount() const;

   // Every parameter the loaded plugin publishes, for the body's fallback
   // "pick a parameter" dropdown - plenty of plugin editors don't notify on
   // every control, and learn-only mapping would leave those unmappable.
   const std::vector<Platform::PluginParamInfo>& AvailableParams() const { return mAvailableParams; }

   Mapping mappings[kMaxMappedParams];

   AudioCable input;
   NoteCable noteInput;

   // Saved identity. `pluginFormat` is always "au" today and exists so a
   // second backend needs no patch-format change.
   std::string pluginFormat;
   std::string pluginId;
   std::string pluginName;

private:
   // Publishes `handle` to the audio thread and retires whatever was live,
   // destroying the generation before that. Main thread only.
   void PublishHandle(Platform::PluginHandle* handle);
   void DestroyAllHandles();
   void RefreshAvailableParams();

   std::unique_ptr<AudioPluginAudioNode> mAudioNode;
   int mLastCookFrame = -1;

   // Set once from the PluginDesc handed to LoadPlugin - true for an
   // instrument or music-effect AU. Drives the note pin's visibility
   // (AcceptsNotes) and never changes without a different plugin being
   // loaded, so it's cheap to cache rather than re-derive every frame.
   bool mAcceptsNotes = false;

   Platform::PluginHandle* mHandle = nullptr;  // the one being loaded / live
   Platform::PluginHandle* mLive = nullptr;    // currently published
   Platform::PluginHandle* mRetired = nullptr; // published one swap ago
   bool mReady = false;
   bool mLoadFailed = false;
   std::string mStatus = "no plugin";

   double mPreparedRate = 0.0;
   int mPreparedBlock = 0;

   bool mConfiguring = false;
   // Base64 fullState. Refreshed from the live plugin inside VisitParams (see
   // its comment) and applied back once a reloaded plugin becomes ready.
   std::string mPluginState;
   bool mPendingStateRestore = false;

   std::vector<Platform::PluginParamInfo> mAvailableParams;
   int mReadbackCursor = 0;
   int mCookCounter = 0;
   float mLastPushed[kMaxMappedParams] = {};
   bool mLastPushedValid[kMaxMappedParams] = {};

public:
   // Bypass is the node's own control (not a plugin param), so it does go
   // through the audio half - it is read inside ProcessBlock.
   bool bypass = false;
};
