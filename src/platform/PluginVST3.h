#pragma once

#include <string>
#include <vector>
#include "platform/Platform.h"

namespace Platform
{
#if INFINITE_ENABLE_VST3
   void EnumerateVST3Plugins(const std::vector<std::string>& folders, std::vector<PluginDesc>& out);
   bool DescribeVST3Bundle(const std::string& bundlePath, std::vector<PluginDesc>& out);
   void CacheVST3BundlePath(const std::string& identifier, const std::string& bundlePath);
   std::string GetCachedVST3BundlePath(const std::string& identifier);

   PluginHandle* PluginVST3Create(const PluginDesc& desc, double sampleRate, int maxBlockFrames);
   PluginLoadState PluginVST3Poll(PluginHandle* handle, std::string& outError);
   bool PluginVST3Prepare(PluginHandle* handle, double sampleRate, int maxBlockFrames, std::string& outError);
   void PluginVST3Destroy(PluginHandle* handle);
   void PluginVST3Render(PluginHandle* handle, const float* const* in, int inChannels,
                         float* const* out, int outChannels, int numFrames);
   void PluginVST3ScheduleMIDIEvent(PluginHandle* handle, int frameOffset, const unsigned char* bytes, int byteCount);

   int PluginVST3ParameterCount(PluginHandle* handle);
   bool PluginVST3ParameterInfo(PluginHandle* handle, int index, PluginParamInfo& out);
   bool PluginVST3ParameterInfoByAddress(PluginHandle* handle, unsigned long long address, PluginParamInfo& out);
   void PluginVST3SetParameter(PluginHandle* handle, unsigned long long address, float value);
   bool PluginVST3GetParameter(PluginHandle* handle, unsigned long long address, float& outValue);

   void PluginVST3BeginLearn(PluginHandle* handle);
   void PluginVST3EndLearn(PluginHandle* handle);
   bool PluginVST3PollLearned(PluginHandle* handle, unsigned long long& outAddress);

   bool PluginVST3OpenEditor(PluginHandle* handle, std::string& outError);
   void PluginVST3CloseEditor(PluginHandle* handle);
   bool PluginVST3EditorIsOpen(PluginHandle* handle);

   bool PluginVST3SaveState(PluginHandle* handle, std::string& outBase64);
   bool PluginVST3RestoreState(PluginHandle* handle, const std::string& base64);
#endif
}
