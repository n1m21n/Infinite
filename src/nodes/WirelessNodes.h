#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/AudioCable.h"
#include "core/GLUtil.h"
#include "core/INode.h"
#include "core/ImageCable.h"
#include "core/Modulation.h"
#include "core/NoteCable.h"
#include "Geometry3DNodes.h"

// Turbo: Pure Data style wireless connections. A Send node takes one cable
// and a channel name; every Receive of the same kind and name gets it, with
// no cable between them. Several Sends on one name fan in at the Receive
// (each Receive kind combines them its own way), and any number of Receives
// can listen to one name.
//
// Sends have no output pin. A Receive never keeps a pointer to a Send across
// frames: image, CV and geometry Receives look their Sends up by name every
// time they are read, and the audio/note Receives hold hidden cables that the
// main loop re-resolves each frame (UpdateWirelessCables in main.cpp), so a
// deleted, renamed or re-patched Send can never leave a stale pointer behind.
class WirelessNode : public INode
{
public:
   enum Kind { kImage, kCV, kNotes, kAudio, kGeometry, kKindCount };

   WirelessNode(Kind kind, bool isSend);
   ~WirelessNode() override;

   Kind GetKind() const { return mKind; }
   bool IsSend() const { return mIsSend; }

   std::string channel = "ch1";

   // Every live Send of `kind` on `name`, oldest first.
   static std::vector<WirelessNode*> Senders(Kind kind, const std::string& name);
   // Every live Receive of `kind` on `name`.
   static std::vector<WirelessNode*> Receivers(Kind kind, const std::string& name);
   // Every channel name in use, sorted, for the name picker.
   static std::vector<std::string> ChannelNames();
   static const char* KindName(Kind kind);

   void VisitParams(ParamVisitor& v) override { v.Text("channel", channel); }

private:
   static std::vector<WirelessNode*>& Registry();
   Kind mKind;
   bool mIsSend;
};

// ---------------------------------------------------------------- image

class SendImageNode : public WirelessNode
{
public:
   static INode* Create() { return new SendImageNode(); }
   SendImageNode() : WirelessNode(kImage, true) {}

   // The preview shows what is being sent.
   unsigned int GetOutputTexture() override { return mInput.Texture(); }
   int GetOutputWidth() const override { return mInput.Width(); }
   int GetOutputHeight() const override { return mInput.Height(); }
   unsigned int GetOutputFormat() const override { return mInput.Format(); }
   unsigned long long TextureRevision() const override { return mInput.Revision(); }
   void CookIfNeeded(int frameId) override { mInput.Pull(frameId); }
   const char* InputLabel(int) const override { return "in"; }

   ImageCable& Input() { return mInput; }

private:
   ImageCable mInput;
};

class ReceiveImageNode : public WirelessNode
{
public:
   enum Mode { kFirst, kIndex, kAdd, kMax, kAverage };
   static constexpr int kMaxSenders = 8;

   static INode* Create() { return new ReceiveImageNode(); }
   static const std::vector<std::string>& ModeNames();
   ReceiveImageNode() : WirelessNode(kImage, false) {}
   ~ReceiveImageNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   unsigned int GetOutputFormat() const override { return mOut.internalFormat; }
   unsigned long long TextureRevision() const override { return mRevision; }
   void CookIfNeeded(int frameId) override;

   int mode = kFirst;
   int index = 0;
   int SenderCount() const { return mSenderCount; }

   void VisitParams(ParamVisitor& v) override
   {
      WirelessNode::VisitParams(v);
      v.Int("mode", mode); v.Int("index", index);
   }

private:
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   int mSenderCount = 0;
   std::vector<unsigned long long> mBuiltSig;
   bool mHasBuilt = false;
   unsigned long long mRevision = 0;
};

// ---------------------------------------------------------------- CV

class SendCVNode : public WirelessNode
{
public:
   static INode* Create() { return new SendCVNode(); }
   SendCVNode() : WirelessNode(kCV, true) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }
   const char* InputLabel(int) const override { return "cv"; }
};

class ReceiveCVNode : public WirelessNode, public IModulator
{
public:
   enum Mode { kFirst, kSum, kAverage, kMax, kMin };

   static INode* Create() { return new ReceiveCVNode(); }
   static const std::vector<std::string>& ModeNames();
   ReceiveCVNode() : WirelessNode(kCV, false) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;
   int SenderCount() const { return mSenderCount; }

   int mode = kFirst;

   void VisitParams(ParamVisitor& v) override
   {
      WirelessNode::VisitParams(v);
      v.Int("mode", mode);
   }

private:
   // A Receive patched (through anything) back into a Send of its own name
   // would otherwise recurse forever; while it is being read it answers
   // its previous value, a one-read delay like Feedback's one frame.
   bool mReading = false;
   float mLast = 0.0f;
   int mSenderCount = 0;
};

// ---------------------------------------------------------------- notes

class SendNotesNode : public WirelessNode
{
public:
   static INode* Create() { return new SendNotesNode(); }
   SendNotesNode() : WirelessNode(kNotes, true) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   NoteCable noteInput;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int) const override { return "notes"; }
};

class AudioWirelessNoteMerge;

class ReceiveNotesNode : public WirelessNode, public INoteSource
{
public:
   static constexpr int kMaxSenders = 8;

   static INode* Create() { return new ReceiveNotesNode(); }
   ReceiveNotesNode();
   ~ReceiveNotesNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override;

   AudioNode* GetAudioNode() override;
   NoteCable* WirelessNoteInput(int i) override { return (i >= 0 && i < kMaxSenders) ? &mWireless[i] : nullptr; }

private:
   NoteCable mWireless[kMaxSenders];
   std::unique_ptr<AudioWirelessNoteMerge> mAudioNode;
};

// ---------------------------------------------------------------- audio

class SendAudioNode : public WirelessNode
{
public:
   static INode* Create() { return new SendAudioNode(); }
   SendAudioNode() : WirelessNode(kAudio, true) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   AudioCable input;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "audio"; }
};

class AudioWirelessSum;

class ReceiveAudioNode : public WirelessNode, public IAudioSource
{
public:
   static constexpr int kMaxSenders = 8;

   static INode* Create() { return new ReceiveAudioNode(); }
   ReceiveAudioNode();
   ~ReceiveAudioNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override;

   AudioNode* GetAudioNode() override;
   AudioCable* WirelessAudioInput(int i) override { return (i >= 0 && i < kMaxSenders) ? &mWireless[i] : nullptr; }

   float gain = 1.0f; // linear, applied to the sum

   void VisitParams(ParamVisitor& v) override
   {
      WirelessNode::VisitParams(v);
      v.Float("gain", gain);
   }

private:
   AudioCable mWireless[kMaxSenders];
   std::unique_ptr<AudioWirelessSum> mAudioNode;
};

// ---------------------------------------------------------------- geometry

class SendGeometryNode : public WirelessNode
{
public:
   static INode* Create() { return new SendGeometryNode(); }
   SendGeometryNode() : WirelessNode(kGeometry, true) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   IGeometrySource* input = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }
};

// Forwards one Send's geometry (the `index`-th on its channel) untouched,
// every accessor included - see Null3DNode for why a pass-through must never
// invent a revision stamp or drop a side channel.
class ReceiveGeometryNode : public WirelessNode, public IGeometrySource
{
public:
   static INode* Create() { return new ReceiveGeometryNode(); }
   ReceiveGeometryNode() : WirelessNode(kGeometry, false) {}

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   const Mesh& GetMesh() override;
   unsigned long long MeshRevision() override;
   Mat4 GetModelMatrix() const override;
   Material GetMaterial() const override;
   unsigned int GetSurfaceTexture() override;
   unsigned int GetMaterialTexture(int map) override;
   unsigned long long SurfaceTextureRevision() const override;
   MappingTransform GetMappingTransform() const override;
   IGeometrySource* PassthroughSource() const override { return Source(); }
   Mat4 GetInstanceGroupMatrix() const override;
   const pop::PopData* GetPop() override { IGeometrySource* s = Source(); return s ? s->GetPop() : nullptr; }
   const pop::PopData* GetPopInstances() override { IGeometrySource* s = Source(); return s ? s->GetPopInstances() : nullptr; }
   const std::vector<Particle>* GetPointCloud() override;
   unsigned long long PointCloudRevision() override;
   float PointBaseSize() const override;
   const Polyline* GetCurve() override;
   unsigned long long CurveStamp() override;

   int index = 0;

   void VisitParams(ParamVisitor& v) override
   {
      WirelessNode::VisitParams(v);
      v.Int("index", index);
   }

   // The Send's upstream geometry this Receive forwards right now, or null
   // (no Send on the channel, or this Receive feeds that Send itself - a
   // wireless cycle, which geometry has no frame delay to break).
   IGeometrySource* Source() const;

private:
   Mesh mEmpty;
   int mLastCookFrame = -1;
};
