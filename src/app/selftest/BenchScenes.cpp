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

// The boat scene (realtime-engine "everything at once" check): a small wooden sloop floating on the
// Ocean at sunset or under the moon, with the sky made here so the sun or moon disc sits exactly where its
// light comes from. INFINITE_BENCH_B4SCALE=boat-sunset | boat-moon; the parts load from
// INFINITE_BENCH_BOATDIR (default bench/assets/boat, made by bench/assets/make_boat.py). BenchBoatTick()
// floats the boat on the waves every frame.
namespace
{
struct BoatState
{
   int oceanIdx = -1;
   std::vector<int> parts;
   int lanternLightIdx = -1;
   int fillIdx = -1, keyIdx = -1, camIdx = -1, renderIdx = -1;
   std::string envPath;
   float y = 0, pitch = 0, roll = 0;
   bool primed = false;
};
BoatState sBoat;

float BoatHash(int x, int y)
{
   unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
   h = (h ^ (h >> 13)) * 1274126177u;
   return (float)((h ^ (h >> 16)) & 0xffffff) / (float)0xffffff;
}
float BoatNoise(float x, float y)
{
   const int xi = (int)std::floor(x), yi = (int)std::floor(y);
   const float fx = x - xi, fy = y - yi;
   const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
   const float a = BoatHash(xi, yi), b = BoatHash(xi + 1, yi), c = BoatHash(xi, yi + 1), d = BoatHash(xi + 1, yi + 1);
   return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}
float BoatFbm(float x, float y)
{
   float v = 0, amp = 0.5f;
   for (int o = 0; o < 5; o++) { v += amp * BoatNoise(x, y); x *= 2.03f; y *= 2.03f; amp *= 0.5f; }
   return v;
}

// Equirect sky in the engine's convention: u = atan2(z, x) / 2pi + 0.5, v = acos(y) / pi, row 0 at the top.
void BoatSky(bool moon, float lightAzDeg, float lightElDeg, const std::string& path)
{
   const int w = 2048, h = 1024;
   std::vector<float> px((size_t)w * h * 3);
   const float az0 = lightAzDeg * 3.14159265f / 180.0f, el0 = lightElDeg * 3.14159265f / 180.0f;
   const float L[3] = { std::cos(el0) * std::cos(az0), std::sin(el0), std::cos(el0) * std::sin(az0) };
   const float discAng = (moon ? 0.9f : 0.8f) * 3.14159265f / 180.0f;
   const float texelAng = 3.14159265f / (float)h;
   // Disc coverage with a one-texel soft edge, so the magnified disc is round rather than stair-stepped.
   auto disc = [&](float c) { return std::max(0.0f, std::min(1.0f, (discAng - std::acos(c)) / texelAng + 0.5f)); };
   // The sky above the horizon at (az, el >= 0); pixel coordinates seed the stars.
   auto sky = [&](float az, float el, int x, int y, float out[3])
   {
      const float d[3] = { std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az) };
      const float c = std::max(-1.0f, std::min(1.0f, d[0] * L[0] + d[1] * L[1] + d[2] * L[2]));
      const float cp = std::max(0.0f, c);
      const float hz = std::max(0.0f, std::cos(az - az0)); // toward the light along the horizon
      float r, g, b;
      if (!moon)
      {
         const float t = std::pow(1.0f - el / 1.5708f, 9.0f);
         const float h2 = hz * hz * hz;
         const float hr = 0.30f + 1.25f * h2, hg = 0.16f + 0.42f * h2, hb = 0.22f + 0.05f * h2;
         r = 0.025f + (hr - 0.025f) * t; g = 0.045f + (hg - 0.045f) * t; b = 0.14f + (hb - 0.14f) * t;
         const float glow = 2.5f * std::pow(cp, 64.0f) + 0.35f * std::pow(cp, 8.0f);
         r += glow * 1.5f; g += glow * 0.58f; b += glow * 0.18f;
         if (el < 0.30f)
         {
            // Low streaks of cloud, lit gold toward the sun, violet-grey away from it.
            const float n = BoatFbm(az * 5.0f, el * 55.0f);
            const float band = std::max(0.0f, 1.0f - std::abs(el - 0.10f) / 0.10f);
            const float k = std::max(0.0f, std::min(1.0f, (n - 0.50f) * 4.0f)) * band;
            const float lit = 0.10f + 2.4f * std::pow(cp, 4.0f) + 0.3f * h2;
            r += (1.0f * lit + 0.05f - r) * k * 0.9f; g += (0.38f * lit + 0.04f - g) * k * 0.9f; b += (0.22f * lit + 0.07f - b) * k * 0.9f;
         }
         const float k = disc(c);
         r += (900.0f - r) * k; g += (520.0f - g) * k; b += (260.0f - b) * k;
      }
      else
      {
         const float t = std::pow(1.0f - el / 1.5708f, 4.0f);
         r = 0.0025f + 0.012f * t; g = 0.0045f + 0.020f * t; b = 0.012f + 0.040f * t;
         const float halo = 0.25f * std::pow(cp, 220.0f) + 0.03f * std::pow(cp, 12.0f);
         r += halo * 0.7f; g += halo * 0.8f; b += halo * 1.0f;
         if (el > 0.04f && BoatHash(x, y) > 0.9975f)
         {
            // Dim enough not to saturate a whole magnified texel; a few brighter ones.
            const float s = 0.03f + 0.5f * std::pow(BoatHash(y, x), 8.0f);
            r += s * 0.9f; g += s * 0.95f; b += s;
         }
         if (el < 0.25f)
         {
            const float n = BoatFbm(az * 4.0f, el * 40.0f);
            const float k = std::max(0.0f, std::min(1.0f, (n - 0.55f) * 3.0f)) * std::max(0.0f, 1.0f - el / 0.25f);
            const float lit = 0.004f + 0.05f * std::pow(cp, 8.0f);
            r += (lit * 0.8f - r) * k; g += (lit * 0.85f - g) * k; b += (lit - b) * k;
         }
         if (const float k = disc(c); k > 0.0f)
         {
            const float m = 0.80f + 0.20f * BoatFbm(az * 600.0f, el * 600.0f); // maria
            r += (30.0f * m * 0.92f - r) * k; g += (30.0f * m * 0.95f - g) * k; b += (30.0f * m - b) * k;
         }
      }
      out[0] = r; out[1] = g; out[2] = b;
   };
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
         const float az = ((x + 0.5f) / w - 0.5f) * 6.2831853f;
         const float el = (0.5f - (y + 0.5f) / h) * 3.14159265f;
         float* o = &px[((size_t)y * w + x) * 3];
         if (el >= 0.0f)
         {
            sky(az, el, x, y, o);
            continue;
         }
         // Below the horizon is the far sea the Ocean mesh does not reach: the sky mirrored by Fresnel over
         // dark water, with the light's glitter path widening toward the viewer.
         float m[3];
         sky(az, -el, x, h - 1 - y, m);
         const float dep = -el;
         const float fres = 0.02f + 0.98f * std::pow(1.0f - std::sin(dep), 5.0f);
         const float deep[3] = { 0.002f, 0.006f, 0.010f };
         float daz = std::fabs(az - az0);
         if (daz > 3.14159265f) daz = 6.2831853f - daz;
         const float width = 0.006f + 0.22f * dep;
         const float jn = BoatFbm(az * 260.0f, dep * 1400.0f);
         const float jitter = 3.0f * jn * jn * jn;
         const float path = std::exp(-(daz * daz) / (width * width)) * std::exp(-dep * 6.0f) * jitter;
         const float sunCol[3] = { moon ? 0.55f : 6.0f, moon ? 0.6f : 2.6f, moon ? 0.7f : 0.9f };
         for (int k = 0; k < 3; k++)
            o[k] = deep[k] * (1 - fres) + std::min(m[k], 4.0f) * fres * 0.85f + sunCol[k] * path;
      }
   stbi_write_hdr(path.c_str(), w, h, 3, px.data());
}

void BuildBenchBoatScene(bool moon, bool isAnim, int& outRender3DIdx, int& outOutputIdx, int& outCamIdx, int& outLfoIdx)
{
   auto nodeAt = [](int idx) { return FindNodeByIndex(idx)->node.get(); };
   const char* dirEnv = getenv("INFINITE_BENCH_BOATDIR");
   const std::string dir = (dirEnv && *dirEnv) ? dirEnv : "bench/assets/boat";

   // Light first: the sky's disc is drawn where it says.
   const float lightAz = moon ? 10.0f : 0.0f, lightEl = moon ? 11.0f : 3.5f;
   const std::string envPath = TmpPath(moon ? "infinite_bench_boat_moon.hdr" : "infinite_bench_boat_sunset.hdr");
   BoatSky(moon, lightAz, lightEl, envPath);

   const int oceanIdx = SpawnNode("Ocean", "3D", 0.0f, 0.0f)->index;
   auto* ocean = static_cast<OceanNode*>(nodeAt(oceanIdx));
   ocean->resolution = 320;
   ocean->size = 40.0f;
   ocean->horizon = 60.0f; // out to 1.2 km: waves to the skyline

   ocean->amplitude = 0.07f;
   ocean->wavelength = 5.0f;
   ocean->steepness = 0.55f;
   ocean->choppiness = 0.8f;
   ocean->octaves = 6;
   ocean->direction = 25.0f;
   ocean->speed = 0.5f;
   ocean->color[0] = 0.006f; ocean->color[1] = 0.020f; ocean->color[2] = 0.032f;
   ocean->roughness = 0.025f;
   ocean->metallic = 0.0f;
   ocean->ior = 1.33f;
   ocean->specular = 0.5f;
   sBoat = BoatState();
   sBoat.oceanIdx = oceanIdx;
   sBoat.envPath = envPath;

   struct Part { const char* file; float r, g, b, metal, rough, clear, emit; };
   const Part parts[] = {
      { "hull", 0.035f, 0.07f, 0.13f, 0.0f, 0.32f, 0.6f, 0.0f },
      { "deck", 0.40f, 0.25f, 0.12f, 0.0f, 0.60f, 0.0f, 0.0f },
      { "cabin", 0.80f, 0.77f, 0.70f, 0.0f, 0.50f, 0.2f, 0.0f },
      { "windows", 0.015f, 0.018f, 0.025f, 0.0f, 0.05f, 0.0f, 0.0f },
      { "trim", 0.36f, 0.20f, 0.08f, 0.0f, 0.35f, 0.4f, 0.0f },
      { "sail", 0.86f, 0.82f, 0.72f, 0.0f, 0.85f, 0.0f, 0.0f },
      { "lantern", 1.0f, 0.62f, 0.28f, 0.0f, 0.30f, 0.0f, moon ? 14.0f : 1.5f },
   };
   const int groupIdx = SpawnNode("Group 3D", "3D", 260.0f, 260.0f)->index;
   auto* group = static_cast<Group3DNode*>(nodeAt(groupIdx));
   int slot = 0;
   for (const Part& p : parts)
   {
      const int idx = SpawnNode("Model 3D", "3D", 0.0f, 260.0f + 120.0f * slot)->index;
      auto* m = static_cast<ModelSourceNode*>(nodeAt(idx));
      m->normalizeScale = false;
      m->recenter = false;
      if (!m->Load(dir + "/" + p.file + ".obj"))
         fprintf(stderr, "BOAT: could not load %s/%s.obj\n", dir.c_str(), p.file);
      m->color[0] = p.r; m->color[1] = p.g; m->color[2] = p.b;
      m->metallic = p.metal;
      m->roughness = p.rough;
      m->clearcoat = p.clear;
      m->emissionColor[0] = 1.0f; m->emissionColor[1] = 0.55f; m->emissionColor[2] = 0.22f;
      m->emission = p.emit;
      m->rotY = 0.35f; // bow a little off the light, so the glitter path runs past the hull
      group->inputs[slot++] = m;
      sBoat.parts.push_back(idx);
   }

   const int camIdx = SpawnNode("Camera", "3D", 520.0f, 0.0f)->index;
   auto* cam = static_cast<CameraNode*>(nodeAt(camIdx));
   cam->distance = moon ? 8.0f : 8.5f;
   cam->elevation = moon ? 7.0f : 5.0f;
   cam->azimuth = lightAz + 180.0f + (moon ? 17.0f : 24.0f);
   cam->targetX = 0.0f; cam->targetY = moon ? 1.4f : 1.5f; cam->targetZ = 0.0f;
   cam->fov = 38.0f;
   cam->farPlane = 2000.0f;
   outCamIdx = camIdx;

   int lightIdx[3] = { -1, -1, -1 };
   lightIdx[0] = SpawnNode("Light", "3D", 520.0f, 260.0f)->index;
   auto* key = static_cast<LightNode*>(nodeAt(lightIdx[0]));
   key->type = 2; // sun
   key->azimuth = lightAz; key->elevation = lightEl;
   if (!moon) { key->color[0] = 1.0f; key->color[1] = 0.55f; key->color[2] = 0.28f; key->intensity = 3.2f; }
   else { key->color[0] = 0.62f; key->color[1] = 0.72f; key->color[2] = 1.0f; key->intensity = 0.55f; }
   key->shadowRadius = moon ? 0.15f : 0.1f;
   lightIdx[1] = SpawnNode("Light", "3D", 520.0f, 520.0f)->index;
   auto* fill = static_cast<LightNode*>(nodeAt(lightIdx[1]));
   fill->type = 3; // ambient: the sky's own wash
   const bool abMode = getenv("INFINITE_BENCH_BOATAB") != nullptr; // Blender A/B: only lights Cycles has
   if (abMode) fill->intensity = 0.0f;
   else if (!moon) { fill->color[0] = 0.45f; fill->color[1] = 0.45f; fill->color[2] = 0.65f; fill->intensity = 0.20f; }
   else { fill->color[0] = 0.25f; fill->color[1] = 0.35f; fill->color[2] = 0.6f; fill->intensity = 0.06f; }
   sBoat.fillIdx = lightIdx[1];
   if (moon)
   {
      // The stern lantern's own light: a warm point riding with the boat.
      lightIdx[2] = SpawnNode("Light", "3D", 520.0f, 780.0f)->index;
      auto* lan = static_cast<LightNode*>(nodeAt(lightIdx[2]));
      lan->type = 1;
      lan->color[0] = 1.0f; lan->color[1] = 0.58f; lan->color[2] = 0.25f;
      lan->intensity = 1.6f;
      lan->falloff = abMode ? 1.0f : 2.0f;
      if (abMode) lan->softFalloff = false;
      lan->castShadow = true;
      sBoat.lanternLightIdx = lightIdx[2];
   }

   const int envIdx = SpawnNode("HDRI", "3D", 520.0f, 1040.0f)->index;
   static_cast<EnvironmentNode*>(nodeAt(envIdx))->Load(envPath);

   const int renderIdx = SpawnNode("Render 3D", "3D", 780.0f, 0.0f)->index;
   auto* render = static_cast<Render3DNode*>(nodeAt(renderIdx));
   render->geometry[0] = ocean;
   render->geometry[1] = group;
   render->camera = cam;
   for (int l = 0; l < 3; l++)
      if (lightIdx[l] >= 0)
         render->lights[l] = static_cast<LightNode*>(nodeAt(lightIdx[l]));
   render->envInput.Connect(nodeAt(envIdx));
   render->width = 1920.0f;
   render->height = 1080.0f;
   render->samples = 2;
   render->tonemap = 3; // AgX: holds the sun and the lantern without clipping to flat white
   render->exposure = moon ? 2.2f : 0.8f;
   render->bloom = abMode ? 0.0f : (moon ? 0.25f : 0.35f);
   if (abMode)
   {
      render->shadowStrength = 1.0f; // Cycles' shadows are full
      render->rimIntensity = 0.0f;   // a stylised edge glow with no Cycles twin
      for (float& c : render->ambientColor) c = 0.6136f; // linear 1/3: sky light at full strength (ambient * 3 == 1), as Cycles' world
   }
   render->shadowsEnabled = true;
   render->shadowQuality = 1;
   outRender3DIdx = renderIdx;
   sBoat.keyIdx = lightIdx[0]; sBoat.camIdx = camIdx; sBoat.renderIdx = renderIdx;

   const int outIdx = SpawnNode("Output", "Utility", 1040.0f, 0.0f)->index;
   if (ImageCable* in = CableFor(*FindNodeByIndex(outIdx), 0))
      in->Connect(nodeAt(renderIdx));
   outOutputIdx = outIdx;

   outLfoIdx = -1; // the camera holds still; the sea and the boat move
   Transport::Instance().SetPlaying(isAnim);
   BenchBoatTick();
   for (GraphNode& gn : gNodes)
      gn.showParams = true;
}
} // namespace

// Floats the boat: the sea height under bow, stern and both sides, from the Ocean's own mesh, eased so the
// hull has some weight. Moves every part (and the lantern's light) together.
void BenchBoatTick()
{
   if (sBoat.oceanIdx < 0 || sBoat.parts.empty())
      return;
   GraphNode* og = FindNodeByIndex(sBoat.oceanIdx);
   if (og == nullptr)
      return;
   auto* ocean = static_cast<OceanNode*>(og->node.get());
   const Mesh& mesh = ocean->GetMesh();
   const float yaw = 0.35f;
   const float cy = std::cos(yaw), sy = std::sin(yaw);
   // Local hull points (x along the keel, z across), turned like the parts (Mat4::RotationY).
   const float probe[4][2] = { { 1.3f, 0.0f }, { -1.3f, 0.0f }, { 0.0f, 0.5f }, { 0.0f, -0.5f } };
   float hgt[4];
   for (int k = 0; k < 4; k++)
   {
      const float lx = probe[k][0], lz = probe[k][1];
      const float wx = cy * lx + sy * lz, wz = -sy * lx + cy * lz;
      float sum = 0, wsum = 0;
      for (const Vertex& v : mesh.vertices)
      {
         const float dx = v.px + ocean->posX - wx, dz = v.pz + ocean->posZ - wz;
         const float d2 = dx * dx + dz * dz;
         if (d2 < 0.09f) { const float wgt = 0.09f - d2; sum += v.py * wgt; wsum += wgt; }
      }
      hgt[k] = (wsum > 0 ? sum / wsum : 0.0f) + ocean->posY;
   }
   const float y = 0.25f * (hgt[0] + hgt[1] + hgt[2] + hgt[3]) - 0.04f;
   const float pitch = std::atan2(hgt[0] - hgt[1], 2.6f) * 0.7f;
   const float roll = std::atan2(hgt[2] - hgt[3], 1.0f) * 0.5f;
   const float ease = sBoat.primed ? 0.12f : 1.0f;
   sBoat.y += (y - sBoat.y) * ease;
   sBoat.pitch += (pitch - sBoat.pitch) * ease;
   sBoat.roll += (roll - sBoat.roll) * ease;
   sBoat.primed = true;
   for (int idx : sBoat.parts)
      if (GraphNode* g = FindNodeByIndex(idx))
      {
         auto* m = static_cast<ModelSourceNode*>(g->node.get());
         m->posY = sBoat.y;
         m->rotZ = sBoat.pitch;
         m->rotX = -sBoat.roll;
      }
   if (sBoat.lanternLightIdx >= 0)
      if (GraphNode* g = FindNodeByIndex(sBoat.lanternLightIdx))
      {
         // The lantern sits on the stern post; place the point light just outboard of it.
         auto* l = static_cast<LightNode*>(g->node.get());
         const float lx = -1.45f, lz = 0.0f;
         const float wx = cy * lx + sy * lz, wz = -sy * lx + cy * lz, wy = sBoat.y + 0.70f;
         const float r = std::sqrt(wx * wx + wy * wy + wz * wz);
         l->distance = r;
         l->elevation = std::asin(wy / r) * 180.0f / 3.14159265f;
         l->azimuth = std::atan2(wz, wx) * 180.0f / 3.14159265f;
      }
}


// Writes the frame as it stands for the Blender A/B (tools/blender-ab/boat_scene.py): the ocean and every boat part
// with their transforms baked in, in Blender's Z-up axes (x, -z, y), and scene.json with the camera, lights,
// materials (colours as the nodes hold them, sRGB) and the render's exposure.
void BenchBoatDump(const std::string& dir)
{
   if (sBoat.oceanIdx < 0)
      return;
   auto node = [](int idx) { GraphNode* g = FindNodeByIndex(idx); return g ? g->node.get() : nullptr; };
   auto writeObj = [&](const std::string& name, const Mesh& mesh, const Mat4& m)
   {
      FILE* f = fopen((dir + "/" + name + ".obj").c_str(), "w");
      if (!f) return;
      for (const Vertex& v : mesh.vertices)
      {
         const float x = m.m[0] * v.px + m.m[4] * v.py + m.m[8] * v.pz + m.m[12];
         const float y = m.m[1] * v.px + m.m[5] * v.py + m.m[9] * v.pz + m.m[13];
         const float z = m.m[2] * v.px + m.m[6] * v.py + m.m[10] * v.pz + m.m[14];
         fprintf(f, "v %.5f %.5f %.5f\n", x, -z, y);
      }
      for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
         fprintf(f, "f %u %u %u\n", mesh.indices[i] + 1, mesh.indices[i + 1] + 1, mesh.indices[i + 2] + 1);
      fclose(f);
   };
   auto matJson = [](const Material& mt) {
      char b[512];
      snprintf(b, sizeof(b), "{\"color\":[%g,%g,%g],\"metallic\":%g,\"roughness\":%g,\"ior\":%g,\"specular\":%g,\"clearcoat\":%g,"
               "\"clearcoat_roughness\":%g,\"emission_color\":[%g,%g,%g],\"emission\":%g}",
               mt.color[0], mt.color[1], mt.color[2], mt.metallic, mt.roughness, mt.ior, mt.specular, mt.clearcoat,
               mt.clearcoatRoughness, mt.emissionColor[0], mt.emissionColor[1], mt.emissionColor[2], mt.emission);
      return std::string(b);
   };
   auto* ocean = static_cast<OceanNode*>(node(sBoat.oceanIdx));
   writeObj("ocean", ocean->GetMesh(), ocean->GetModelMatrix());
   std::string json = "{\"objects\":[{\"name\":\"ocean\",\"material\":" + matJson(ocean->GetMaterial()) + "}";
   static const char* kParts[] = { "hull", "deck", "cabin", "windows", "trim", "sail", "lantern" };
   for (size_t i = 0; i < sBoat.parts.size() && i < 7; i++)
   {
      auto* mdl = static_cast<ModelSourceNode*>(node(sBoat.parts[i]));
      writeObj(kParts[i], mdl->GetMesh(), mdl->GetModelMatrix());
      json += std::string(",{\"name\":\"") + kParts[i] + "\",\"material\":" + matJson(mdl->GetMaterial()) + "}";
   }
   json += "],";
   auto* cam = static_cast<CameraNode*>(node(sBoat.camIdx));
   float eye[3];
   cam->ComputeEye(eye);
   char b[1024];
   snprintf(b, sizeof(b), "\"camera\":{\"eye\":[%g,%g,%g],\"target\":[%g,%g,%g],\"fov\":%g},", eye[0], -eye[2], eye[1],
            cam->targetX, -cam->targetZ, cam->targetY, cam->fov);
   json += b;
   json += "\"lights\":[";
   bool first = true;
   for (int idx : { sBoat.keyIdx, sBoat.fillIdx, sBoat.lanternLightIdx })
   {
      if (idx < 0) continue;
      auto* l = static_cast<LightNode*>(node(idx));
      float v[3];
      l->ComputeVector(v);
      snprintf(b, sizeof(b), "%s{\"type\":%d,\"vec\":[%g,%g,%g],\"color\":[%g,%g,%g],\"intensity\":%g,\"falloff\":%g,\"soft\":%d,\"angle\":%g}",
               first ? "" : ",", l->type, v[0], -v[2], v[1], l->color[0], l->color[1], l->color[2], l->intensity, l->falloff,
               l->softFalloff ? 1 : 0, 0.526f * std::max(0.0f, l->shadowRadius) / 0.25f * std::max(0.0f, l->shadowFilter));
      json += b;
      first = false;
   }
   auto* r = static_cast<Render3DNode*>(node(sBoat.renderIdx));
   snprintf(b, sizeof(b), "],\"exposure\":%g,\"tonemap\":%d,\"bloom\":%g,\"env\":\"%s\",\"size\":[%d,%d]}", r->exposure, r->tonemap,
            r->bloom, sBoat.envPath.c_str(), (int)r->width, (int)r->height);
   json += b;
   if (FILE* f = fopen((dir + "/scene.json").c_str(), "w")) { fputs(json.c_str(), f); fclose(f); }
   printf("BOATDUMP %s\n", dir.c_str());
}

void BuildBenchB4Scene(const std::string& scaleStr, const std::string& shadowStr, bool isAnim, int& outRender3DIdx, int& outOutputIdx, int& outCamIdx, int& outLfoIdx)
{
   if (scaleStr == "boat-sunset" || scaleStr == "boat-moon")
   {
      BuildBenchBoatScene(scaleStr == "boat-moon", isAnim, outRender3DIdx, outOutputIdx, outCamIdx, outLfoIdx);
      return;
   }
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

   // Beat-reactive reference scene (realtime-engine R0): everything that can move on the beat does, so the
   // render is judged on a scene like the Milestone 1 demo. The clock is the transport's beats.
   if (getenv("INFINITE_BENCH_B4MUSIC") != nullptr)
   {
      torus->spinY = 0.5f;
      glassGeo->spinY = 0.25f;
      for (int l = 0; l < 3; l++)
         static_cast<LightNode*>(nodeAt(lightIdx[l]))->orbitPerBeat = 15.0f * (float)(l + 1);
      cam->orbitPerBeat = 5.0f;
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

bool BuildBenchB11Scene(const std::string& scene, int n, const std::string& clipPath,
                        int& outRender3DIdx, int& outOutputIdx, int& outPointsIdx, std::string& outError)
{
   auto nodeAt = [](int idx) { return FindNodeByIndex(idx)->node.get(); };
   n = std::max(1, n);

   // The point chain's last node, whatever the scene; Render 3D draws it.
   IGeometrySource* chainEnd = nullptr;

   if (scene == "a" || scene == "d")
   {
      // A moving picture re-cooks Image to Points every frame (readback +
      // rebuild + upload), and Transform's spin moves the whole cloud on top:
      // the "animated points" worst case on main. Displacement and Field
      // Modifier pass a point cloud through untouched, so neither can be the
      // animated stage here.
      GraphNode* vidGn = SpawnNode("Video", "Source", 0.0f, 0.0f);
      if (vidGn == nullptr) { outError = "could not spawn Video node"; return false; }
      auto* vid = static_cast<VideoSourceNode*>(vidGn->node.get());
      vid->loop = true;
      if (!vid->Open(clipPath)) { outError = "could not open " + clipPath + ": " + vid->LastError(); return false; }

      const int ptsIdx = SpawnNode("Image to Points", "3D", 260.0f, 0.0f)->index;
      auto* pts = static_cast<ImageToPointsNode*>(nodeAt(ptsIdx));
      pts->Input().Connect(vid);
      // Square grid, capped at 512 by the node (262,144 points).
      pts->density = std::max(2, std::min(512, (int)std::lround(std::sqrt((double)n))));
      pts->threshold = 0.0f; // keep every cell, so the count is the grid
      pts->width = 3.2f;
      pts->height = 1.8f;
      outPointsIdx = ptsIdx;

      const int xfIdx = SpawnNode("Transform", "3D", 520.0f, 0.0f)->index;
      auto* xf = static_cast<GeometryOpNode*>(nodeAt(xfIdx));
      xf->input = pts;
      xf->op = GeometryOpNode::kTransform;
      xf->spin = 0.25f;
      chainEnd = xf;

      if (scene == "d")
      {
         // The same moving cloud stamped with cubes: Instance on Points
         // rebuilds a matrix per point every frame unless it reads the
         // cloud's GPU points (P8 step 6).
         const int cubeIdx = SpawnNode("Cube", "3D", 520.0f, 260.0f)->index;
         const int instIdx = SpawnNode("Instance on Points", "3D", 780.0f, 260.0f)->index;
         auto* inst = static_cast<InstanceOnPointsNode*>(nodeAt(instIdx));
         inst->cloudSource = xf;
         inst->instanceShape = static_cast<GeometryNode*>(nodeAt(cubeIdx));
         inst->instanceScale = 0.8f;
         chainEnd = inst;
      }
   }
   else if (scene == "b" || scene == "c")
   {
      const int psIdx = SpawnNode("Particle System", "3D", 0.0f, 0.0f)->index;
      auto* ps = static_cast<ParticleSystemNode*>(nodeAt(psIdx));
      // Steady state: births per second x life = cap, with 5% headroom so the
      // cap (not the rate) sets the count. The harness waits for it to fill.
      ps->maxParticles = n;
      ps->lifetime = 4.0f;
      ps->lifetimeRandom = 0.0f;
      ps->emitRate = (float)n / ps->lifetime * 1.05f;
      ps->emitRadius = 0.5f;
      ps->turbulence = 0.6f;
      ps->gravityY = -0.3f;
      outPointsIdx = psIdx;
      chainEnd = ps;

      if (scene == "c")
      {
         const int cubeIdx = SpawnNode("Cube", "3D", 0.0f, 260.0f)->index;
         const int instIdx = SpawnNode("Instance on Points", "3D", 260.0f, 0.0f)->index;
         auto* inst = static_cast<InstanceOnPointsNode*>(nodeAt(instIdx));
         inst->cloudSource = ps;
         inst->instanceShape = static_cast<GeometryNode*>(nodeAt(cubeIdx));
         inst->instanceScale = 0.5f;
         chainEnd = inst;
      }
   }
   else
   {
      outError = "unknown B11 scene '" + scene + "' (a, b, c or d)";
      return false;
   }

   const int camIdx = SpawnNode("Camera", "3D", 520.0f, 260.0f)->index;
   auto* cam = static_cast<CameraNode*>(nodeAt(camIdx));
   cam->distance = 5.0f;
   cam->elevation = 15.0f;
   cam->azimuth = 30.0f;

   const int lightIdx = SpawnNode("Light", "3D", 520.0f, 520.0f)->index;
   auto* light = static_cast<LightNode*>(nodeAt(lightIdx));
   light->intensity = 1.8f;

   const int renderIdx = SpawnNode("Render 3D", "3D", 780.0f, 0.0f)->index;
   auto* render = static_cast<Render3DNode*>(nodeAt(renderIdx));
   render->geometry[0] = chainEnd;
   render->camera = cam;
   render->lights[0] = light;
   render->width = 1920.0f;
   render->height = 1080.0f;
   outRender3DIdx = renderIdx;

   // Cost-split knobs (P8): which part of the GPU time is fill, samples or
   // shading. INFINITE_BENCH_B11_SAMPLES is Render 3D's sample index (0 off,
   // 2 = 4x, the default); INFINITE_BENCH_B11_SIZE multiplies the point size.
   if (const char* s = getenv("INFINITE_BENCH_B11_SAMPLES"))
      render->samples = std::max(0, std::min(3, atoi(s)));
   if (const char* s = getenv("INFINITE_BENCH_B11_SIZE"))
   {
      const float k = std::max(0.01f, (float)atof(s));
      if (auto* pts = dynamic_cast<ImageToPointsNode*>(nodeAt(outPointsIdx)))
         pts->pointSize *= k;
      if (auto* ps = dynamic_cast<ParticleSystemNode*>(nodeAt(outPointsIdx)))
      {
         ps->startSize *= k;
         ps->endSize *= k;
      }
   }
   // Instance cost split (c, d): INFINITE_BENCH_B11_DETAIL is the stamped
   // Cube's detail (24 = 108 triangles, 8 = 12), INFINITE_BENCH_B11_ISCALE
   // multiplies the stamp size, INFINITE_BENCH_B11_RES the render size.
   if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(chainEnd))
   {
      if (const char* s = getenv("INFINITE_BENCH_B11_DETAIL"))
         if (auto* cube = dynamic_cast<GeometryNode*>(inst->instanceShape))
            cube->detail = std::max(1, atoi(s));
      if (const char* s = getenv("INFINITE_BENCH_B11_ISCALE"))
         inst->instanceScale *= std::max(0.01f, (float)atof(s));
   }
   if (const char* s = getenv("INFINITE_BENCH_B11_RES"))
   {
      const float k = std::max(0.05f, std::min(1.0f, (float)atof(s)));
      render->width = std::round(1920.0f * k);
      render->height = std::round(1080.0f * k);
   }

   const int outIdx = SpawnNode("Output", "Utility", 1040.0f, 0.0f)->index;
   if (ImageCable* in = CableFor(*FindNodeByIndex(outIdx), 0))
      in->Connect(nodeAt(renderIdx));
   outOutputIdx = outIdx;

   for (GraphNode& gn : gNodes)
      gn.showParams = true;
   // The point node's own sliders clamp past what a user can reach (Image to
   // Points density 400 = 160k points, max particles 50,000), and a drawn
   // slider writes its clamp back. Collapse that one body so the engine's own
   // caps (512², 200k) are what the run measures.
   if (GraphNode* gn = FindNodeByIndex(outPointsIdx))
      gn->showParams = false;
   Transport::Instance().SetPlaying(true);
   return true;
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
   if (n > 1000) cols = (int)std::ceil(std::sqrt((double)n * 1.33)); // R5 4000-node variant: keep the grid near square
   else if (n >= 400) cols = 24;
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
