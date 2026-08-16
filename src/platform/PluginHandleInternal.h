#pragma once

#include "Platform.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

@interface InfinitePluginEditorDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) void* handle;
@end

namespace Platform
{
   struct PluginHandle
   {
      PluginDesc desc;

      // --- instantiation, main thread except where noted -------------------
      AUAudioUnit* unit = nil;
      AUAudioUnit* arrivedUnit = nil; // written by the completion handler's queue
      NSError* arrivedError = nil;    // ditto
      std::mutex arrivalMutex;        // guards the two above; main thread only takes it in PluginPoll
      std::atomic<bool> arrived { false };
      PluginLoadState state = PluginLoadState::Pending;
      std::string loadError;

      // --- render path -----------------------------------------------------
      AURenderBlock renderBlockStrong = nil;
      void* renderBlockRaw = nullptr;
      AUScheduleMIDIEventBlock scheduleMIDIEventBlockStrong = nil;
      void* scheduleMIDIEventBlockRaw = nullptr;

      double sampleRate = 0.0;
      int maxBlockFrames = 0;
      bool resourcesAllocated = false;
      int pluginInChannels = 0;
      int pluginOutChannels = 0;

      AudioBufferList* outAbl = nullptr;   // allocated once at prepare, sized kPluginMaxChannels
      std::vector<float> inScratch;        // kPluginMaxChannels * maxBlockFrames
      std::vector<float> outScratch;       // ditto, used only when channel counts differ
      double renderSampleTime = 0.0;
      std::atomic<bool> inRender { false };

      // --- learn -----------------------------------------------------------
      AUParameterObserverToken learnToken = nullptr;
      std::atomic<bool> learning { false };
      std::atomic<unsigned long long> learnedAddress { 0 };
      std::atomic<bool> learnedValid { false };

      // --- editor window ---------------------------------------------------
      NSWindow* editorWindow = nil;
      NSViewController* editorController = nil;
      InfinitePluginEditorDelegate* editorDelegate = nil;
      bool editorRequestInFlight = false;
      std::atomic<bool> editorOpen { false };
      id editorSizeObserver = nil;
      bool editorUserResized = false;
      bool programmaticResize = false;
   };

   extern std::atomic<int> gOpenPluginEditorCount;
   void SetPluginEditorOpen(PluginHandle* h, bool open);
}
