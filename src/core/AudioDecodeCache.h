#pragma once

#include <string>

#include "Platform.h"

// Shared decode-and-cache entry point for every sample-based audio node
// (AudioFileNode/AnalyzeNodes, SamplerNode, GranularNode, PaulStretchNode,
// MolderNode, GrainMolderNode, DrumSequencerNode) - see
// docs/plans/undo-delete-perf-prompt.md Part B. They all otherwise call
// Platform::DecodeAudioFileToBuffer directly; this wraps that one call with
// an AssetCache<Platform::SampleBuffer> so respawning a node that points at
// the same file (undo/redo, duplicate) doesn't re-decode it from disk.
namespace AudioDecodeCache
{
   bool DecodeCached(const std::string& path, Platform::SampleBuffer& outBuffer, std::string& outError);
}
