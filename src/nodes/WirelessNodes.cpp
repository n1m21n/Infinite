#include "WirelessNodes.h"

#include <algorithm>
#include <atomic>
#include <set>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/NoteEventQueue.h"
#include "platform/OpenGLHeaders.h"

// ================================================================= registry

std::vector<WirelessNode*>& WirelessNode::Registry()
{
   static std::vector<WirelessNode*> registry;
   return registry;
}

WirelessNode::WirelessNode(Kind kind, bool isSend)
: mKind(kind)
, mIsSend(isSend)
{
   Registry().push_back(this);
}

WirelessNode::~WirelessNode()
{
   auto& r = Registry();
   r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

std::vector<WirelessNode*> WirelessNode::Senders(Kind kind, const std::string& name)
{
   // A bypassed Send is muted: nothing on its channel hears it.
   std::vector<WirelessNode*> out;
   for (WirelessNode* n : Registry())
      if (n->mIsSend && !n->bypassed && n->mKind == kind && n->channel == name)
         out.push_back(n);
   return out;
}

std::vector<WirelessNode*> WirelessNode::Receivers(Kind kind, const std::string& name)
{
   std::vector<WirelessNode*> out;
   for (WirelessNode* n : Registry())
      if (!n->mIsSend && n->mKind == kind && n->channel == name)
         out.push_back(n);
   return out;
}

std::vector<std::string> WirelessNode::ChannelNames()
{
   std::set<std::string> names;
   for (WirelessNode* n : Registry())
      if (!n->channel.empty())
         names.insert(n->channel);
   return std::vector<std::string>(names.begin(), names.end());
}

const char* WirelessNode::KindName(Kind kind)
{
   static const char* kNames[kKindCount] = { "image", "CV", "notes", "audio", "geometry" };
   return (kind >= 0 && kind < kKindCount) ? kNames[kind] : "?";
}

// ==================================================================== image

namespace
{
   const char* kReceiveImageFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uT0;\n"
      "uniform sampler2D uT1;\n"
      "uniform sampler2D uT2;\n"
      "uniform sampler2D uT3;\n"
      "uniform sampler2D uT4;\n"
      "uniform sampler2D uT5;\n"
      "uniform sampler2D uT6;\n"
      "uniform sampler2D uT7;\n"
      "uniform int uCount;\n"
      "uniform int uOp;\n" // ReceiveImageNode::Mode; First/Index arrive as one texture
      "vec4 comb(vec4 a, vec4 b) { return uOp == 3 ? max(a, b) : a + b; }\n"
      "void main() {\n"
      "   vec4 acc = texture(uT0, vUv);\n"
      "   if (uCount > 1) acc = comb(acc, texture(uT1, vUv));\n"
      "   if (uCount > 2) acc = comb(acc, texture(uT2, vUv));\n"
      "   if (uCount > 3) acc = comb(acc, texture(uT3, vUv));\n"
      "   if (uCount > 4) acc = comb(acc, texture(uT4, vUv));\n"
      "   if (uCount > 5) acc = comb(acc, texture(uT5, vUv));\n"
      "   if (uCount > 6) acc = comb(acc, texture(uT6, vUv));\n"
      "   if (uCount > 7) acc = comb(acc, texture(uT7, vUv));\n"
      "   if (uOp == 4) acc /= float(max(uCount, 1));\n"
      "   fragColor = acc;\n"
      "}\n";
}

const std::vector<std::string>& ReceiveImageNode::ModeNames()
{
   static const std::vector<std::string> k = { "First", "Index", "Add", "Max", "Average" };
   return k;
}

ReceiveImageNode::~ReceiveImageNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

void ReceiveImageNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   // Set before pulling: a Send fed (through anything) by this Receive pulls
   // it again, and must get last frame's output instead of recursing - the
   // same one-frame delay a Feedback node gives a wired loop. Always
   // rendering into our own target (never handing out a Send's texture)
   // is what keeps that loop from reading and writing one texture at once.
   mLastCookFrame = frameId;

   std::vector<WirelessNode*> senders = Senders(kImage, channel);
   unsigned int tex[kMaxSenders] = {};
   ImageCable* cables[kMaxSenders] = {};
   int count = 0;
   for (WirelessNode* s : senders)
   {
      if (count == kMaxSenders)
         break;
      ImageCable& in = static_cast<SendImageNode*>(s)->Input();
      const unsigned int t = in.Pull(frameId);
      if (t == 0)
         continue;
      tex[count] = t;
      cables[count] = &in;
      count++;
   }
   mSenderCount = count;
   if (count == 0)
   {
      GLUtil::DestroyFbo(mOut);
      mHasBuilt = false;
      return;
   }

   int first = 0;
   int used = count;
   if (mode == kFirst || mode == kIndex)
   {
      first = mode == kIndex ? std::clamp(index, 0, count - 1) : 0;
      used = 1;
   }

   if (!mShaderTried)
   {
      mShaderTried = true;
      mProgram = GLUtil::CompileProgram(kReceiveImageFrag);
   }
   if (mProgram == 0)
      return;

   const int w = std::max(1, cables[first]->Width());
   const int h = std::max(1, cables[first]->Height());
   unsigned int format = cables[first]->Format();
   if (format != GL_RGBA8 && format != GL_RGBA16F && format != GL_RGBA32F)
      format = GL_RGBA8;
   if (!GLUtil::EnsureFbo(mOut, w, h, format))
      return;

   std::vector<unsigned long long> sig = { (unsigned long long)mode, (unsigned long long)first,
                                           (unsigned long long)used, (unsigned long long)w,
                                           (unsigned long long)h, format };
   for (int i = 0; i < used; i++)
      sig.push_back(cables[first + i]->Revision());
   if (mHasBuilt && sig == mBuiltSig)
      return;
   NodeWorkCounter()++;

   GLUtil::RunShaderPass(mOut, mProgram, [this, &tex, first, used]()
   {
      static const char* kNames[kMaxSenders] = { "uT0", "uT1", "uT2", "uT3", "uT4", "uT5", "uT6", "uT7" };
      for (int i = 0; i < kMaxSenders; i++)
      {
         glActiveTexture(GL_TEXTURE0 + i);
         glBindTexture(GL_TEXTURE_2D, i < used ? tex[first + i] : tex[first]);
         glUniform1i(glGetUniformLocation(mProgram, kNames[i]), i);
      }
      glActiveTexture(GL_TEXTURE0);
      glUniform1i(glGetUniformLocation(mProgram, "uCount"), used);
      glUniform1i(glGetUniformLocation(mProgram, "uOp"), mode);
   });
   mBuiltSig = std::move(sig);
   mHasBuilt = true;
   mRevision = NextTextureRevision();
}

// ======================================================================= CV

const std::vector<std::string>& ReceiveCVNode::ModeNames()
{
   static const std::vector<std::string> k = { "First", "Sum", "Average", "Max", "Min" };
   return k;
}

float ReceiveCVNode::Value01()
{
   if (mReading)
      return mLast;
   mReading = true;
   float acc = 0.0f;
   int count = 0;
   for (WirelessNode* s : Senders(kCV, channel))
   {
      IModulator* in = static_cast<SendCVNode*>(s)->input;
      if (in == nullptr)
         continue;
      const float v = in->Value01();
      if (count == 0)
         acc = v;
      else if (mode == kMax)
         acc = std::max(acc, v);
      else if (mode == kMin)
         acc = std::min(acc, v);
      else if (mode == kSum || mode == kAverage)
         acc += v;
      count++;
      if (mode == kFirst)
         break;
   }
   mSenderCount = count;
   if (mode == kAverage && count > 0)
      acc /= (float)count;
   mLast = std::clamp(acc, 0.0f, 1.0f);
   mReading = false;
   return mLast;
}

// ==================================================================== notes

// Merges every connected Send's note stream into one, in timestamp order -
// Note Merge's audio node with one inbox per Send instead of per pin.
class AudioWirelessNoteMerge : public AudioNode
{
public:
   static constexpr int kSlots = ReceiveNotesNode::kMaxSenders;

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& /*output*/) override
   {
      NoteEvent all[kSlots * 64];
      int total = 0;
      for (int s = 0; s < kSlots; s++)
      {
         if (mInbox[s] == nullptr)
            continue;
         NoteEvent evts[64];
         const int n = mInbox[s]->Pop(mCursor[s], evts, 64);
         for (int i = 0; i < n && total < kSlots * 64; i++)
            all[total++] = evts[i];
      }
      std::stable_sort(all, all + total, [](const NoteEvent& a, const NoteEvent& b)
      {
         return a.frameOffset < b.frameOffset;
      });
      for (int i = 0; i < total; i++)
      {
         NoteEvent out = all[i];
         out.source = this;
         mOutbox.Push(out);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   void SetNoteInbox(int inputSlot, NoteEventQueue* inbox, int cursor) override
   {
      if (inputSlot >= 0 && inputSlot < kSlots)
      {
         mInbox[inputSlot] = inbox;
         mCursor[inputSlot] = cursor;
      }
   }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox[kSlots] = {};
   int mCursor[kSlots] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};

ReceiveNotesNode::ReceiveNotesNode() : WirelessNode(kNotes, false) {}
ReceiveNotesNode::~ReceiveNotesNode() = default;

void ReceiveNotesNode::CookIfNeeded(int)
{
   GetAudioNode();
}

AudioNode* ReceiveNotesNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWirelessNoteMerge>();
   return mAudioNode.get();
}

// ==================================================================== audio

// Sums every connected Send's buffer (one topology input each) and scales by
// gain. The main thread only ever writes mGain.
class AudioWirelessSum : public AudioNode
{
public:
   std::atomic<float> mGain{ 1.0f };

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const float g = mGain.load(std::memory_order_relaxed);
      for (int ch = 0; ch < output.numChannels; ch++)
      {
         float* out = output.channels[ch];
         std::fill(out, out + output.numFrames, 0.0f);
         for (int s = 0; s < numInputs; s++)
         {
            const AudioBuffer* in = inputs[s];
            if (in == nullptr || in->numChannels == 0)
               continue;
            // A mono Send feeds every output channel.
            const float* src = in->channels[std::min(ch, in->numChannels - 1)];
            const int frames = std::min(output.numFrames, in->numFrames);
            for (int i = 0; i < frames; i++)
               out[i] += src[i] * g;
         }
      }
   }
};

ReceiveAudioNode::ReceiveAudioNode() : WirelessNode(kAudio, false) {}
ReceiveAudioNode::~ReceiveAudioNode() = default;

void ReceiveAudioNode::CookIfNeeded(int)
{
   static_cast<AudioWirelessSum*>(GetAudioNode())->mGain.store(gain, std::memory_order_relaxed);
}

AudioNode* ReceiveAudioNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioWirelessSum>();
   return mAudioNode.get();
}

// ================================================================= geometry

void SendGeometryNode::CookIfNeeded(int frameId)
{
   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);
}

namespace
{
   // Whether `target` is upstream of `from` through geometry pins or other
   // geometry Receives. Bounded so a malformed graph can never hang the walk.
   bool GeometryReaches(IGeometrySource* from, const INode* target, std::vector<const INode*>& seen)
   {
      auto* node = dynamic_cast<INode*>(from);
      if (node == nullptr)
         return false;
      if (node == target)
         return true;
      if (std::find(seen.begin(), seen.end(), node) != seen.end() || seen.size() > 256)
         return false;
      seen.push_back(node);
      for (int slot = 0; slot < 16; slot++)
      {
         IGeometrySource** field = node->GeometryInputSlot(slot);
         if (field != nullptr && *field != nullptr && GeometryReaches(*field, target, seen))
            return true;
      }
      if (auto* recv = dynamic_cast<ReceiveGeometryNode*>(node))
      {
         auto senders = WirelessNode::Senders(WirelessNode::kGeometry, recv->channel);
         for (WirelessNode* s : senders)
            if (GeometryReaches(static_cast<SendGeometryNode*>(s)->input, target, seen))
               return true;
      }
      return false;
   }
}

IGeometrySource* ReceiveGeometryNode::Source() const
{
   std::vector<WirelessNode*> senders = Senders(kGeometry, channel);
   if (senders.empty())
      return nullptr;
   IGeometrySource* src = static_cast<SendGeometryNode*>(senders[std::clamp(index, 0, (int)senders.size() - 1)])->input;
   std::vector<const INode*> seen;
   if (src == nullptr || GeometryReaches(src, this, seen))
      return nullptr;
   return src;
}

void ReceiveGeometryNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   if (auto* upstream = dynamic_cast<INode*>(Source()))
      upstream->CookIfNeeded(frameId);
}

const Mesh& ReceiveGeometryNode::GetMesh()
{
   IGeometrySource* s = Source();
   return s ? s->GetMesh() : mEmpty;
}

unsigned long long ReceiveGeometryNode::MeshRevision()
{
   IGeometrySource* s = Source();
   return s ? s->MeshRevision() : 0;
}

Mat4 ReceiveGeometryNode::GetModelMatrix() const
{
   IGeometrySource* s = Source();
   return s ? s->GetModelMatrix() : Mat4::Identity();
}

Material ReceiveGeometryNode::GetMaterial() const
{
   IGeometrySource* s = Source();
   return s ? s->GetMaterial() : Material();
}

unsigned int ReceiveGeometryNode::GetSurfaceTexture()
{
   IGeometrySource* s = Source();
   return s ? s->GetSurfaceTexture() : 0;
}

unsigned int ReceiveGeometryNode::GetMaterialTexture(int map)
{
   IGeometrySource* s = Source();
   return s ? s->GetMaterialTexture(map) : 0;
}

unsigned long long ReceiveGeometryNode::SurfaceTextureRevision() const
{
   IGeometrySource* s = Source();
   return s ? s->SurfaceTextureRevision() : 0;
}

MappingTransform ReceiveGeometryNode::GetMappingTransform() const
{
   IGeometrySource* s = Source();
   return s ? s->GetMappingTransform() : MappingTransform();
}

Mat4 ReceiveGeometryNode::GetInstanceGroupMatrix() const
{
   IGeometrySource* s = Source();
   return s ? s->GetInstanceGroupMatrix() : Mat4::Identity();
}

const std::vector<Particle>* ReceiveGeometryNode::GetPointCloud()
{
   IGeometrySource* s = Source();
   return s ? s->GetPointCloud() : nullptr;
}

unsigned long long ReceiveGeometryNode::PointCloudRevision()
{
   IGeometrySource* s = Source();
   return s ? s->PointCloudRevision() : 0;
}

float ReceiveGeometryNode::PointBaseSize() const
{
   IGeometrySource* s = Source();
   return s ? s->PointBaseSize() : 1.0f;
}

const Polyline* ReceiveGeometryNode::GetCurve()
{
   IGeometrySource* s = Source();
   return s ? s->GetCurve() : nullptr;
}

unsigned long long ReceiveGeometryNode::CurveStamp()
{
   IGeometrySource* s = Source();
   return s ? s->CurveStamp() : 0;
}
