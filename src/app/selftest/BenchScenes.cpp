// Benchmark scene builders (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// Fills the bench report's four xrun fields as "since `base`" - each bench
// window baselines the counters at its start so device-open settling does
// not count against the run. audioXruns (deadline + os) is the gated number.
void BenchFillXruns(Bench::BenchReport& report, const AudioEngine::XrunCounts& base)
{
   const AudioEngine::XrunCounts now = AudioEngine::Instance().Xruns();
   report.audioXrunsDeadline = now.deadline - base.deadline;
   report.audioXrunsOs = now.os - base.os;
   report.audioXrunGaps = now.gaps - base.gaps;
   report.audioXruns = report.audioXrunsDeadline + report.audioXrunsOs;
}

void BuildBenchB1Audio(long numVoices, int bufferFrames, float yOffset)
{
   numVoices = std::max(1L, std::min(64L, numVoices));
   static const char* kVoiceTypes[] = { "Sampler", "Wavetable", "Oscillator" };
   const std::string samplerWav = TmpPath("infinite_bench_b1_voice.wav");
   {
      const int fixtureFrames = 2205;
      std::vector<int16_t> fixturePcm(fixtureFrames);
      for (int i = 0; i < fixtureFrames; i++)
      {
         const float t = (float)i / (float)(fixtureFrames - 1);
         fixturePcm[i] = (int16_t)(sinf(t * 30.0f) * 30000.0f);
      }
      std::ofstream f(samplerWav, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(fixturePcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(44100); writeU32(44100 * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)fixturePcm.data(), dataSize);
   }

   GraphNode* lfoGn = SpawnNode("LFO", "Modulators", -260.0f, yOffset);
   const int lfoIdx = lfoGn->index;

   std::vector<int> voiceOutIdx;
   voiceOutIdx.reserve(numVoices);
   const int perRow = 6;
   for (long v = 0; v < numVoices; v++)
   {
      const float x = (float)(v % perRow) * 260.0f;
      const float y = yOffset + (float)(v / perRow) * 900.0f;
      const int srcType = (int)(v % 3);
      const int srcIdx = SpawnNode(kVoiceTypes[srcType], "Synths", x, y)->index;
      if (srcType == 0)
      {
         if (auto* sampler = dynamic_cast<SamplerNode*>(FindNodeByIndex(srcIdx)->node.get()))
            sampler->LoadFile(samplerWav);
      }
      const int filterIdx = SpawnNode("Audio Filter", "AudioEffects", x, y + 150.0f)->index;
      const int shaperIdx = SpawnNode("Wavetable Shaper", "AudioEffects", x, y + 300.0f)->index;
      const int delayIdx  = SpawnNode("Delay", "AudioEffects", x, y + 450.0f)->index;
      const int reverbIdx = SpawnNode("Reverb", "AudioEffects", x, y + 600.0f)->index;
      const int dynIdx    = SpawnNode("Dynamics", "AudioEffects", x, y + 750.0f)->index;

      static_cast<AudioEffectNode*>(FindNodeByIndex(filterIdx)->node.get())->input.Connect(FindNodeByIndex(srcIdx)->node.get());
      static_cast<AudioEffectNode*>(FindNodeByIndex(shaperIdx)->node.get())->input.Connect(FindNodeByIndex(filterIdx)->node.get());
      static_cast<AudioEffectNode*>(FindNodeByIndex(delayIdx)->node.get())->input.Connect(FindNodeByIndex(shaperIdx)->node.get());
      static_cast<AudioEffectNode*>(FindNodeByIndex(reverbIdx)->node.get())->input.Connect(FindNodeByIndex(delayIdx)->node.get());
      static_cast<AudioEffectNode*>(FindNodeByIndex(dynIdx)->node.get())->input.Connect(FindNodeByIndex(reverbIdx)->node.get());

      Modulation::Instance().Bind(filterIdx, /*paramIndex=mix*/ 0, lfoIdx, /*outputIndex=*/ 0);

      voiceOutIdx.push_back(dynIdx);
   }

   std::vector<int> mixerOutIdx;
   for (size_t base = 0; base < voiceOutIdx.size(); base += 12)
   {
      const size_t chunk = std::min((size_t)12, voiceOutIdx.size() - base);
      const int mixIdx = SpawnNode("Mixer", "Utility", 2400.0f, yOffset + (float)(base / 12) * 900.0f)->index;
      auto* mix = static_cast<MixerNode*>(FindNodeByIndex(mixIdx)->node.get());
      mix->numChannels = (int)chunk;
      for (size_t s = 0; s < chunk; s++)
         mix->AudioInputSlot((int)s)->Connect(FindNodeByIndex(voiceOutIdx[base + s])->node.get());
      mixerOutIdx.push_back(mixIdx);
   }

   int finalOutSrcIdx;
   if (mixerOutIdx.size() == 1)
   {
      finalOutSrcIdx = mixerOutIdx[0];
   }
   else
   {
      const int finalMixIdx = SpawnNode("Mixer", "Utility", 2700.0f, yOffset)->index;
      auto* finalMix = static_cast<MixerNode*>(FindNodeByIndex(finalMixIdx)->node.get());
      finalMix->numChannels = (int)mixerOutIdx.size();
      for (size_t s = 0; s < mixerOutIdx.size(); s++)
         finalMix->AudioInputSlot((int)s)->Connect(FindNodeByIndex(mixerOutIdx[s])->node.get());
      finalOutSrcIdx = finalMixIdx;
   }

   const int audioOutIdx = SpawnNode("Audio Out", "Utility", 3000.0f, yOffset)->index;
   static_cast<AudioOutputNode*>(FindNodeByIndex(audioOutIdx)->node.get())->input.Connect(FindNodeByIndex(finalOutSrcIdx)->node.get());

   for (GraphNode& gn : gNodes)
      gn.showParams = true;

   // Always the system default output, never the persisted device choice:
   // a saved AudioObjectID goes stale when the device is replugged, and
   // AudioDeviceOpen then fails outright (-10875) instead of falling back -
   // see perf README "Found while measuring". Engine-only; the saved
   // gAudioOutputDeviceId is left alone so nothing is written back.
   if (AudioEngine::Instance().SampleRate() > 0.0)
      AudioEngine::Instance().Stop();
   AudioEngine::Instance().SetRequestedDevice(0);
   if (bufferFrames > 0)
      AudioEngine::Instance().SetRequestedBufferFrames(bufferFrames);
   if (AudioEngine::Instance().SampleRate() <= 0.0 && !StartAudioEngine(gAudioStartError))
      fprintf(stderr, "BENCH: audio engine did not start: %s\n", gAudioStartError.c_str());
   RebuildAudioTopology();
}

void BuildBenchB2Scene(const std::string& scaleStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx,
                              int* outTwistIdx, int* outMatIdx, int* outCamIdx,
                              bool useEmbossForGlitch)
{
   int effectCount = 10;
   int triDetail = 30;
   if (scaleStr == "s" || scaleStr == "small" || scaleStr == "10") { effectCount = 10; triDetail = 30; }
   else if (scaleStr == "m" || scaleStr == "medium" || scaleStr == "20") { effectCount = 20; triDetail = 60; }
   else if (scaleStr == "l" || scaleStr == "large" || scaleStr == "30") { effectCount = 30; triDetail = 120; }
   else if (atoi(scaleStr.c_str()) > 1) {
      effectCount = atoi(scaleStr.c_str());
      if (effectCount <= 10) { triDetail = 30; }
      else if (effectCount <= 20) { triDetail = 60; }
      else { triDetail = 120; }
   }

   const float xBase = 0.0f;
   const float yBase = 0.0f;

   int torusIdx = SpawnNode("Torus", "3D", xBase, yBase)->index;
   auto* torus = static_cast<GeometryNode*>(FindNodeByIndex(torusIdx)->node.get());
   torus->sides = triDetail;
   torus->detail = triDetail;

   int opIdx = SpawnNode("Twist", "3D", xBase + 260.0f, yBase)->index;
   auto* op = static_cast<GeometryOpNode*>(FindNodeByIndex(opIdx)->node.get());
   op->input = torus;
   op->op = GeometryOpNode::kTwist;
   op->amount = 1.5f;
   if (outTwistIdx) *outTwistIdx = opIdx;

   int matIdx = SpawnNode("Material", "3D", xBase + 520.0f, yBase)->index;
   auto* mat = static_cast<MaterialNode*>(FindNodeByIndex(matIdx)->node.get());
   mat->input = op;
   mat->roughness = 0.35f;
   mat->metallic = 0.65f;
   mat->color[0] = 0.85f;
   mat->color[1] = 0.45f;
   mat->color[2] = 0.20f;
   if (outMatIdx) *outMatIdx = matIdx;

   int camIdx = SpawnNode("Camera", "3D", xBase + 520.0f, yBase + 260.0f)->index;
   auto* cam = static_cast<CameraNode*>(FindNodeByIndex(camIdx)->node.get());
   cam->distance = 4.2f;
   cam->elevation = 20.0f;
   cam->azimuth = 45.0f;
   if (outCamIdx) *outCamIdx = camIdx;

   int lightIdx = SpawnNode("Light", "3D", xBase + 520.0f, yBase + 520.0f)->index;
   auto* light = static_cast<LightNode*>(FindNodeByIndex(lightIdx)->node.get());
   light->intensity = 1.8f;

   int renderIdx = SpawnNode("Render 3D", "3D", xBase + 780.0f, yBase)->index;
   auto* render = static_cast<Render3DNode*>(FindNodeByIndex(renderIdx)->node.get());
   render->geometry[0] = mat;
   render->camera = cam;
   render->lights[0] = light;
   render->width = 1920.0f;
   render->height = 1080.0f;
   outRender3DIdx = renderIdx;

   if (scaleStr == "m" || scaleStr == "l")
   {
      int sphereIdx = SpawnNode("Sphere", "3D", xBase, yBase + 260.0f)->index;
      auto* sphere = static_cast<GeometryNode*>(FindNodeByIndex(sphereIdx)->node.get());
      sphere->sides = triDetail;
      sphere->detail = triDetail;

      int ptsIdx = SpawnNode("Mesh to Points", "3D", xBase + 260.0f, yBase + 260.0f)->index;
      auto* pts = static_cast<MeshToPointsNode*>(FindNodeByIndex(ptsIdx)->node.get());
      pts->input = sphere;

      int cubeIdx = SpawnNode("Cube", "3D", xBase + 260.0f, yBase + 520.0f)->index;
      auto* cube = static_cast<GeometryNode*>(FindNodeByIndex(cubeIdx)->node.get());

      int instIdx = SpawnNode("Instance on Points", "3D", xBase + 520.0f, yBase + 780.0f)->index;
      auto* inst = static_cast<InstanceOnPointsNode*>(FindNodeByIndex(instIdx)->node.get());
      inst->pointSource = pts;
      inst->instanceShape = cube;
      inst->instanceScale = 0.05f;
      inst->maxPoints = (scaleStr == "l") ? 8000 : 2000;
      render->geometry[1] = inst;
   }

   struct EffectDef {
      const char* name;
      const char* cat;
   };
   static const EffectDef kEffectDefs[] = {
      { "gaussianblur", "Effects" },
      { "color adjustments", "Compositing" },
      { "bloom", "Effects" },
      { "vignette", "Effects" },
      { "diffuseglow", "Effects" },
      { "glitch", "Effects" },
      { "lensdistortion", "Effects" },
      { "pixelate", "Effects" },
      { "twirl", "Effects" },
      { "invert", "Compositing" },
      { "posterize", "Compositing" },
      { "threshold", "Compositing" },
   };
   const int kNumEffectTypes = sizeof(kEffectDefs) / sizeof(kEffectDefs[0]);

   int prevNodeIdx = renderIdx;
   float curX = xBase + 1040.0f;
   float curY = yBase;

   int lfoIdx = -1;
   if (isAnim)
   {
      lfoIdx = SpawnNode("LFO", "Modulators", xBase - 260.0f, yBase)->index;
      Modulation::Instance().Bind(opIdx, 0, lfoIdx, 0);
      Modulation::Instance().Bind(camIdx, 0, lfoIdx, 0);
   }

   for (int i = 0; i < effectCount; i++)
   {
      const EffectDef& eff = kEffectDefs[i % kNumEffectTypes];
      const char* effName = ((!isAnim || useEmbossForGlitch) && strcmp(eff.name, "glitch") == 0) ? "emboss" : eff.name;
      const float nodeX = curX + (float)(i % 8) * 260.0f;
      const float nodeY = curY + (float)(i / 8) * 200.0f;

      int effIdx = SpawnNode(effName, eff.cat, nodeX, nodeY)->index;
      if (GraphNode* curGn = FindNodeByIndex(effIdx))
      {
         if (GraphNode* prevGn = FindNodeByIndex(prevNodeIdx))
         {
            if (ImageCable* in = CableFor(*curGn, 0))
               in->Connect(prevGn->node.get());
         }
      }

      if (isAnim && lfoIdx >= 0)
         Modulation::Instance().Bind(effIdx, 0, lfoIdx, 0);

      prevNodeIdx = effIdx;
   }

   const float outX = curX + (float)(effectCount % 8) * 260.0f + 260.0f;
   const float outY = curY + (float)(effectCount / 8) * 200.0f;
   int outIdx = SpawnNode("Output", "Utility", outX, outY)->index;
   if (GraphNode* outGn = FindNodeByIndex(outIdx))
   {
      if (GraphNode* prevGn = FindNodeByIndex(prevNodeIdx))
      {
         if (ImageCable* in = CableFor(*outGn, 0))
            in->Connect(prevGn->node.get());
      }
   }
   outOutputIdx = outIdx;

   for (GraphNode& gn : gNodes)
      gn.showParams = true;
}

void BuildBenchB4Scene(const std::string& scaleStr, const std::string& shadowStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx, int& outCamIdx, int& outLfoIdx)
{
   int instances = 1000, arrayCount = 8, oceanRes = 96, shellDetail = 60;
   if (scaleStr == "m") { instances = 5000; arrayCount = 24; oceanRes = 160; shellDetail = 80; }
   else if (scaleStr == "l") { instances = 20000; arrayCount = 64; oceanRes = 256; shellDetail = 120; }

   int shadowQ = 1;
   bool shadowOn = true;
   if (shadowStr == "off" || shadowStr == "0") { shadowOn = false; }
   else if (shadowStr == "1024") shadowQ = 0;
   else if (shadowStr == "4096") shadowQ = 2;

   const int ew = 1024, eh = 512;
   std::vector<float> envPixels((size_t)ew * eh * 3);
   for (int y = 0; y < eh; y++)
   {
      const float v = (float)y / (float)(eh - 1); // 0 = top
      for (int x = 0; x < ew; x++)
      {
         float* px = &envPixels[((size_t)y * ew + x) * 3];
         const float sky = std::max(0.0f, 1.0f - 2.0f * v);
         const float ground = std::max(0.0f, 2.0f * v - 1.0f);
         px[0] = 0.10f + 0.15f * sky - 0.06f * ground;
         px[1] = 0.12f + 0.25f * sky - 0.07f * ground;
         px[2] = 0.15f + 0.45f * sky - 0.10f * ground;
         const int dx = x - ew / 3, dy = y - eh / 5;
         if (dx * dx + dy * dy < 36)
            px[0] = px[1] = px[2] = 60.0f;
      }
   }
   const std::string envPath = TmpPath("infinite_bench_b4_env.hdr");
   stbi_write_hdr(envPath.c_str(), ew, eh, 3, envPixels.data());

   auto nodeAt = [](int idx) { return FindNodeByIndex(idx)->node.get(); };

   const int oceanIdx = SpawnNode("Ocean", "3D", 0.0f, 0.0f)->index;
   auto* ocean = static_cast<OceanNode*>(nodeAt(oceanIdx));
   ocean->resolution = oceanRes;
   ocean->uniformScale = 3.0f;
   ocean->posY = -0.6f;

   const int shellIdx = SpawnNode("Sphere", "3D", 0.0f, 260.0f)->index;
   auto* shell = static_cast<GeometryNode*>(nodeAt(shellIdx));
   shell->sides = shellDetail;
   shell->detail = shellDetail;
   shell->uniformScale = 1.4f;
   shell->posY = 0.9f;
   const int cubeIdx = SpawnNode("Cube", "3D", 0.0f, 520.0f)->index;
   const int instIdx = SpawnNode("Instance on Points", "3D", 260.0f, 260.0f)->index;
   auto* inst = static_cast<InstanceOnPointsNode*>(nodeAt(instIdx));
   inst->pointSource = static_cast<GeometryNode*>(nodeAt(shellIdx));
   inst->instanceShape = static_cast<GeometryNode*>(nodeAt(cubeIdx));
   inst->pointMode = 2; // faces
   inst->maxPoints = instances;
   inst->instanceScale = (scaleStr == "l") ? 0.035f : (scaleStr == "m" ? 0.05f : 0.08f);
   inst->inheritMaterial = false;
   inst->metallic = 0.3f;
   inst->roughness = 0.35f;

   const int torusIdx = SpawnNode("Torus", "3D", 0.0f, 780.0f)->index;
   auto* torus = static_cast<GeometryNode*>(nodeAt(torusIdx));
   torus->sides = 24;
   torus->detail = 32;
   torus->uniformScale = 0.35f;
   const int arrIdx = SpawnNode("Array", "3D", 260.0f, 780.0f)->index;
   auto* arr = static_cast<GeometryOpNode*>(nodeAt(arrIdx));
   arr->input = torus;
   arr->op = GeometryOpNode::kArray;
   arr->count = arrayCount;
   arr->radial = true;
   arr->radius = 2.6f;
   const int metalIdx = SpawnNode("Material", "3D", 520.0f, 780.0f)->index;
   auto* metal = static_cast<MaterialNode*>(nodeAt(metalIdx));
   metal->input = arr;
   metal->metallic = 1.0f;
   metal->roughness = 0.18f;
   metal->color[0] = 0.95f; metal->color[1] = 0.78f; metal->color[2] = 0.45f;

   const int glassGeoIdx = SpawnNode("Sphere", "3D", 0.0f, 1040.0f)->index;
   auto* glassGeo = static_cast<GeometryNode*>(nodeAt(glassGeoIdx));
   glassGeo->sides = 48;
   glassGeo->detail = 48;
   glassGeo->uniformScale = 0.7f;
   glassGeo->posX = -2.2f;
   glassGeo->posY = 0.5f;
   glassGeo->posZ = 1.2f;
   const int glassIdx = SpawnNode("Material", "3D", 260.0f, 1040.0f)->index;
   auto* glass = static_cast<MaterialNode*>(nodeAt(glassIdx));
   glass->input = glassGeo;
   glass->transmission = 0.95f;
   glass->roughness = 0.05f;
   glass->transmissionRoughness = 0.1f;

   const int camIdx = SpawnNode("Camera", "3D", 520.0f, 0.0f)->index;
   auto* cam = static_cast<CameraNode*>(nodeAt(camIdx));
   cam->distance = 7.0f;
   cam->elevation = 24.0f;
   cam->azimuth = 30.0f;
   cam->targetY = 0.5f;
   outCamIdx = camIdx;

   static const int kLightTypes[] = { 2 /*sun*/, 1 /*point*/, 4 /*spot*/ };
   int lightIdx[3];
   for (int l = 0; l < 3; l++)
   {
      lightIdx[l] = SpawnNode("Light", "3D", 520.0f, 260.0f + 260.0f * l)->index;
      auto* light = static_cast<LightNode*>(nodeAt(lightIdx[l]));
      light->type = kLightTypes[l];
      light->azimuth = 40.0f + 110.0f * l;
      light->elevation = 55.0f - 10.0f * l;
      light->intensity = (l == 0) ? 2.0f : 1.2f;
   }

   const int envIdx = SpawnNode("HDRI", "3D", 520.0f, 1040.0f)->index;
   static_cast<EnvironmentNode*>(nodeAt(envIdx))->Load(envPath);

   const int renderIdx = SpawnNode("Render 3D", "3D", 780.0f, 0.0f)->index;
   auto* render = static_cast<Render3DNode*>(nodeAt(renderIdx));
   render->geometry[0] = static_cast<OceanNode*>(nodeAt(oceanIdx));
   render->geometry[1] = static_cast<InstanceOnPointsNode*>(nodeAt(instIdx));
   render->geometry[2] = static_cast<MaterialNode*>(nodeAt(metalIdx));
   render->geometry[3] = static_cast<MaterialNode*>(nodeAt(glassIdx));
   render->camera = static_cast<CameraNode*>(nodeAt(camIdx));
   for (int l = 0; l < 3; l++)
      render->lights[l] = static_cast<LightNode*>(nodeAt(lightIdx[l]));
   render->envInput.Connect(nodeAt(envIdx));
   render->width = 1920.0f;
   render->height = 1080.0f;
   render->samples = 2; // 4x
   render->tonemap = 1; // ACES
   render->shadowsEnabled = shadowOn;
   render->shadowQuality = shadowQ;
   outRender3DIdx = renderIdx;

   const int outIdx = SpawnNode("Output", "Utility", 1040.0f, 0.0f)->index;
   if (ImageCable* in = CableFor(*FindNodeByIndex(outIdx), 0))
      in->Connect(nodeAt(renderIdx));
   outOutputIdx = outIdx;

   if (isAnim)
   {
      outLfoIdx = SpawnNode("LFO", "Modulators", 520.0f, -260.0f)->index;
      Transport::Instance().SetPlaying(true);
   }
   else
   {
      outLfoIdx = -1;
      Transport::Instance().SetPlaying(false);
   }

   for (GraphNode& gn : gNodes)
      gn.showParams = true;
}

void BuildBenchB6Scene(int n, bool collapsed, float& outMaxX, float& outMaxY, int& outDragNodeIdx)
{
   struct B6Type { const char* type; const char* cat; };
   static const B6Type kTypes[] = {
      { "Shape", "Source" },
      { "Noise", "Source" },
      { "invert", "Compositing" },
      { "gaussianblur", "Effects" },
      { "Math", "Modulators" },
      { "Audio Filter", "Audio" },
      { "Delay", "Audio" },
      { "Reverb", "Audio" },
      { "LFO", "Modulators" },
      { "Color Ramp", "Effects" },
      { "Range to Range", "Modulators" },
      { "Constant", "Modulators" }
   };
   const int kTypeCount = (int)(sizeof(kTypes) / sizeof(kTypes[0]));

   int cols = 16;
   if (n >= 400) cols = 24;
   else if (n >= 300) cols = 20;
   else if (n >= 200) cols = 16;
   else cols = std::max(4, (int)std::ceil(std::sqrt((double)n * 1.33)));

   const float stepX = 280.0f;
   const float stepY = 240.0f;
   outMaxX = 0.0f;
   outMaxY = 0.0f;

   std::vector<int> spawnedIndices;
   spawnedIndices.reserve(n);

   for (int i = 0; i < n; i++)
   {
      const int t = i % kTypeCount;
      const float x = (float)(i % cols) * stepX;
      const float y = (float)(i / cols) * stepY;
      if (x > outMaxX) outMaxX = x;
      if (y > outMaxY) outMaxY = y;

      GraphNode* gn = SpawnNode(kTypes[t].type, kTypes[t].cat, x, y);
      if (gn)
         spawnedIndices.push_back(gn->index);
   }

   // Chain adjacent nodes with cables
   for (size_t i = 1; i < spawnedIndices.size(); i++)
   {
      GraphNode* prev = FindNodeByIndex(spawnedIndices[i - 1]);
      GraphNode* curr = FindNodeByIndex(spawnedIndices[i]);
      if (!prev || !curr || !prev->node || !curr->node) continue;

      WireInputSlot(*prev, *curr, 0, 0);
   }

   for (GraphNode& gn : gNodes)
      gn.showParams = !collapsed;

   if (!spawnedIndices.empty())
      outDragNodeIdx = spawnedIndices[0];

   // Stop transport so pure canvas cost is measured, not cooking
   Transport::Instance().SetPlaying(false);
}

// B8 Media I/O scene (docs/plans/perf/benchmark-suite.md §4): `clips` Video
// nodes, each on its own Output; optionally a Syphon Out (Spout on Windows)
// fed by clip 0 and a Video In camera on its own Output. Indices, not
// GraphNode*s, come back - SpawnNode push_backs onto gNodes, so any pointer
// taken before a later spawn dangles.
bool BuildBenchB8Scene(const std::vector<std::string>& clipPaths, bool withSyphon, bool withCamera, int windows,
                              std::vector<int>& outClipIdx, std::vector<int>& outOutputIdx,
                              int& outSyphonIdx, int& outCameraIdx, std::string& outError)
{
   outClipIdx.clear();
   outOutputIdx.clear();
   outSyphonIdx = -1;
   outCameraIdx = -1;

   for (size_t i = 0; i < clipPaths.size(); i++)
   {
      const float y = (float)i * 220.0f;
      GraphNode* vidGn = SpawnNode("Video", "Source", 0.0f, y);
      if (vidGn == nullptr)
      {
         outError = "could not spawn Video node";
         return false;
      }
      const int vidIdx = vidGn->index;
      auto* vid = static_cast<VideoSourceNode*>(vidGn->node.get());
      vid->loop = true;
      if (!vid->Open(clipPaths[i]))
      {
         outError = "could not open " + clipPaths[i] + ": " + vid->LastError();
         return false;
      }
      const int outIdx = SpawnNode("Output", "Utility", 320.0f, y)->index;
      if (GraphNode* outGn = FindNodeByIndex(outIdx))
         if (GraphNode* src = FindNodeByIndex(vidIdx))
            WireInputSlot(*src, *outGn, 0, 0);
      outClipIdx.push_back(vidIdx);
      outOutputIdx.push_back(outIdx);
   }

   // One projector window per Output node (OpenProjectorWindow refuses a
   // second window on the same node), so windows beyond the clip count get
   // extra Outputs on clips 0, 1, ... - the same texture shown twice.
   for (int w = (int)outClipIdx.size(); w < windows && !outClipIdx.empty(); w++)
   {
      const int srcIdx = outClipIdx[(size_t)w % outClipIdx.size()];
      const int outIdx = SpawnNode("Output", "Utility", 320.0f, (float)w * 220.0f + 110.0f)->index;
      if (GraphNode* outGn = FindNodeByIndex(outIdx))
         if (GraphNode* src = FindNodeByIndex(srcIdx))
            WireInputSlot(*src, *outGn, 0, 0);
      outOutputIdx.push_back(outIdx);
   }

   if (withSyphon && !outClipIdx.empty())
   {
      outSyphonIdx = SpawnNode("Syphon Out", "Utility", 640.0f, 0.0f)->index;
      if (GraphNode* syGn = FindNodeByIndex(outSyphonIdx))
         if (GraphNode* src = FindNodeByIndex(outClipIdx[0]))
            WireInputSlot(*src, *syGn, 0, 0);
   }

   if (withCamera)
   {
      const float y = (float)clipPaths.size() * 220.0f;
      outCameraIdx = SpawnNode("Video In", "Source", 0.0f, y)->index;
      const int camOutIdx = SpawnNode("Output", "Utility", 320.0f, y)->index;
      if (GraphNode* outGn = FindNodeByIndex(camOutIdx))
         if (GraphNode* src = FindNodeByIndex(outCameraIdx))
            WireInputSlot(*src, *outGn, 0, 0);
   }

   for (GraphNode& gn : gNodes)
      gn.showParams = true;
   Transport::Instance().SetPlaying(true);
   return true;
}
}
