// Generate Samples. See GenerateSamplesNode.h.
#include "app/graph/GenerateSamplesNode.h"

#include "app/graph/OfflineGraph.h"
#include "audio/AudioFileWriter.h"
#include "core/AudioSummary.h"
#include "core/Patch.h"
#include "core/INode.h"
#include "audio/MusicTime.h"
#include "core/Transport.h"
#include "platform/Platform.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <random>
#include <thread>

namespace
{
   constexpr double kRate = 48000.0;
   constexpr int kMaxRedraws = 3;
   constexpr double kTargetLufs = -14.0;
   constexpr double kCeilingDb = -1.0;  // true peak
   constexpr double kSilentLufs = -50.0;

   int SnapBars(int bars)
   {
      return bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : 8;
   }
}

struct GenerateSamplesNode::Job
{
   SampleStyles::Recipe recipe; // as asked; attempts move only the seed
   int attempt = 0;
   int barFrames = 0;
   int prerollBars = 0;
   std::unique_ptr<app::OfflineGraph> graph;
   std::thread worker;
   std::atomic<bool> cancel { false };
   std::atomic<bool> done { false };
   std::atomic<float> progress { 0.0f };
   bool rendered = false;
   std::vector<float> left, right;

   void Join()
   {
      if (worker.joinable())
         worker.join();
   }
   ~Job()
   {
      cancel.store(true);
      Join();
   }
};

GenerateSamplesNode::GenerateSamplesNode()
{
   loop = true;
}

GenerateSamplesNode::~GenerateSamplesNode() = default;

SampleStyles::Recipe GenerateSamplesNode::CurrentRecipe() const
{
   SampleStyles::Recipe r;
   r.style = style;
   r.seed = (uint32_t)std::max(0, seed);
   r.variation = variation;
   r.bars = SnapBars(bars);
   if (followTransport)
   {
      r.bpm = Transport::Instance().Tempo();
      r.key = Transport::Instance().Key();
      r.scale = Transport::Instance().Scale();
   }
   r.part = (SampleStyles::Part)std::clamp(part, 0, (int)SampleStyles::Part::Count - 1);
   return r;
}

GenerateSamplesNode::State GenerateSamplesNode::GetState() const
{
   if (mJob)
      return State::Generating;
   if (mFailed)
      return State::Failed;
   if (!mHasSample)
      return State::Empty;
   const SampleStyles::Recipe r = CurrentRecipe();
   const bool same = r.style == mMade.style && r.seed == mMade.seed && std::fabs(r.variation - mMade.variation) < 1e-4f &&
                     r.bars == mMade.bars && std::fabs(r.bpm - mMade.bpm) < 0.01f && r.key == mMade.key &&
                     r.scale == mMade.scale && r.part == mMade.part;
   return same ? State::Ready : State::Stale;
}

float GenerateSamplesNode::Progress() const
{
   return mJob ? mJob->progress.load(std::memory_order_relaxed) : 0.0f;
}

void GenerateSamplesNode::RollSeed()
{
   std::random_device rd;
   seed = (int)(rd() % 100000u);
}

void GenerateSamplesNode::Cancel()
{
   mJob.reset(); // cancels and joins
}

void GenerateSamplesNode::Generate()
{
   Cancel();
   mFailed = false;
   mJob = std::make_unique<Job>();
   mJob->recipe = CurrentRecipe();
   if (!StartAttempt(0))
   {
      mJob.reset();
      mFailed = true;
   }
}

// Build and prepare on this (the main) thread, render on a worker.
bool GenerateSamplesNode::StartAttempt(int attempt)
{
   Job& job = *mJob;
   job.attempt = attempt;
   SampleStyles::Recipe r = job.recipe;
   r.seed += (uint32_t)attempt * 7919u; // a redraw: the same card, another seed
   const SampleStyles::Compiled c = SampleStyles::Compile(r);
   if (!c.error.empty())
      return false;
   Patch::Data data;
   std::string err;
   if (!Patch::ReadText(c.patch, data, err))
      return false;
   job.graph = std::make_unique<app::OfflineGraph>();
   if (!job.graph->Build(data, c.tap))
      return false;
   for (const app::OfflineGraph::Motion& m : c.motions)
      job.graph->AddMotion(m);
   if (!job.graph->Prepare(kRate))
      return false;
   madeBpm = c.bpm;
   madeRoot = c.root;
   madeScale = c.scale;

   // A pre-roll of one drum pattern (2 bars; 1 for a 1-bar loop) renders
   // first and is dropped: the loop then starts with the reverb and release
   // tails a loop playing round would have, so the seam does not click in.
   job.barFrames = (int)std::lround(4.0 * 60.0 / std::max(20.0f, c.bpm) * kRate);
   job.prerollBars = PrerollBars(r.bars);
   const int frames = job.barFrames * (r.bars + job.prerollBars);
   job.done.store(false);
   job.progress.store(0.0f);
   Job* jp = &job;
   job.worker = std::thread([jp, frames] {
      jp->rendered = jp->graph->Render(frames, jp->left, jp->right, &jp->cancel, &jp->progress);
      jp->done.store(true, std::memory_order_release);
   });
   return true;
}

// The render is back: gate it, normalise it, hand it to the player.
void GenerateSamplesNode::Finish()
{
   Job& job = *mJob;
   job.Join();
   job.graph.reset();
   if (!job.rendered)
   {
      mJob.reset();
      return;
   }
   const int from = job.barFrames * job.prerollBars;
   const int frames = job.barFrames * job.recipe.bars;
   if (!Master(job.left.data() + from, job.right.data() + from, frames, mLoopL, mLoopR))
   {
      if (job.attempt < kMaxRedraws && StartAttempt(job.attempt + 1))
         return;
      mJob.reset();
      mFailed = true;
      return;
   }
   auto* buf = new Platform::SampleBuffer();
   buf->channels = 2;
   buf->numFrames = frames;
   buf->sampleRate = kRate;
   buf->channelData.resize((size_t)frames * 2);
   for (int i = 0; i < frames; i++)
   {
      buf->channelData[i] = mLoopL[i];
      buf->channelData[(size_t)frames + i] = mLoopR[i];
   }

   const SampleStyles::Recipe made = job.recipe;
   mJob.reset();
   mMade = made;
   FinishBuffer(buf, MadeLabel(), "", "generated");
   if (mRestoreRange)
   {
      start = mSavedStart;
      end = mSavedEnd;
      position = mSavedPosition;
      mRestoreRange = false;
   }
   mHasSample = true;
   mGenerated = true;
   mGenVersion = kGeneratorVersion;
}

bool GenerateSamplesNode::Master(const float* l, const float* r, int frames, std::vector<float>& outL, std::vector<float>& outR)
{
   AudioSummary::Analyzer an(kRate);
   an.Push(l, r, frames);
   const AudioSummary::Result s = an.Finish();
   const double rms = std::sqrt(0.5 * (s.rms[0] * s.rms[0] + s.rms[1] * s.rms[1]));
   const bool finite = s.nonFiniteFrames == 0;
   const bool silent = !(s.integratedLufs > kSilentLufs); // NaN for silence
   const bool flat = rms > 0.0 && s.samplePeak / rms < 1.2; // DC or a stuck square, not music
   if (!finite || silent || flat)
      return false;
   // -14 LUFS, then held under a -1 dBTP ceiling.
   double gain = std::pow(10.0, (kTargetLufs - s.integratedLufs) / 20.0);
   const double ceiling = std::pow(10.0, kCeilingDb / 20.0);
   if (s.truePeak * gain > ceiling)
      gain = ceiling / s.truePeak;
   outL.assign(l, l + frames);
   outR.assign(r, r + frames);
   for (int i = 0; i < frames; i++)
   {
      outL[i] *= (float)gain;
      outR[i] *= (float)gain;
   }
   return true;
}

std::string GenerateSamplesNode::Label(const SampleStyles::Recipe& r, int root, int scale)
{
   static const char* kRoots[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
   const SampleStyles::Style st = SampleStyles::At(std::max(0, SampleStyles::Find(r.style)));
   std::string s = st.name;
   if (r.part != SampleStyles::Part::Full)
      s += std::string(" ") + SampleStyles::PartName(r.part);
   s += " " + std::to_string(r.seed);
   // Drums and percussion have no key.
   if (r.part != SampleStyles::Part::Drums && r.part != SampleStyles::Part::Percussion)
      s += std::string(", ") + kRoots[((root % 12) + 12) % 12] + " " + MusicTime::ScaleTable(scale).name;
   return s;
}

std::string GenerateSamplesNode::MadeLabel() const
{
   return Label(mMade, madeRoot, madeScale);
}

bool GenerateSamplesNode::KeepAsFile()
{
   if (mLoopL.empty())
      return false;
   std::vector<float> inter(mLoopL.size() * 2);
   for (size_t i = 0; i < mLoopL.size(); i++)
   {
      inter[i * 2] = mLoopL[i];
      inter[i * 2 + 1] = mLoopR[i];
   }
   // The file says what it is: generated-deep-house-drums-1204-...
   std::string prefix = "generated " + MadeLabel();
   for (char& ch : prefix)
      ch = std::isalnum((unsigned char)ch) ? ch : ch == '#' ? 's' : '-';
   prefix.erase(std::unique(prefix.begin(), prefix.end(), [](char a, char b) { return a == '-' && b == '-'; }), prefix.end());
   const std::string path = AudioRecordings::GenerateFilePath(prefix);
   if (!AudioRecordings::WriteWav(path, inter.data(), (int)mLoopL.size(), kRate, 2))
      return false;
   const float s0 = start, e0 = end, p0 = position;
   if (!LoadFile(path))
      return false;
   start = s0;
   end = e0;
   position = p0;
   return true;
}

void GenerateSamplesNode::CookIfNeeded(int frameId)
{
   SamplerNode::CookIfNeeded(frameId);
   if (mJob && mJob->done.load(std::memory_order_acquire))
      Finish();
   // A kept file reloaded from a patch: it is the sample, made from the saved recipe.
   if (!mHasSample && !mJob && !mFilePath.empty())
   {
      mHasSample = true;
      mMade = CurrentRecipe();
   }
   // Reopened from a patch: render the saved recipe again.
   if (mGenerated && !mHasSample && !mJob && !mFailed)
   {
      mRestoreRange = true;
      mSavedStart = start;
      mSavedEnd = end;
      mSavedPosition = position;
      Generate();
   }
}

void GenerateSamplesNode::VisitParams(ParamVisitor& v)
{
   SamplerNode::VisitParams(v);
   v.Text("style", style);
   v.Int("seed", seed);
   v.Float("variation", variation);
   v.Int("bars", bars);
   v.Bool("followTransport", followTransport);
   v.Int("part", part);
   v.Bool("generated", mGenerated);
   v.Int("genVersion", mGenVersion); // not a control: the generator that made the saved sample
   seed = std::clamp(seed, 0, 99999);
   variation = std::clamp(variation, 0.0f, 1.0f);
   bars = SnapBars(bars);
   part = std::clamp(part, 0, (int)SampleStyles::Part::Count - 1);
   if (SampleStyles::Find(style) < 0)
      style = "deep_house";
}
