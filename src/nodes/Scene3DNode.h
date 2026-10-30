#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "INode.h"
#include "Geometry3DNodes.h"
#include "GLUtil.h"

class CameraNode;
class LightNode;

// The 3D View's scene: node outputs placed in one space and rendered as one
// picture. An item only references a node (by uid) and adds a placement on top
// of whatever that node outputs, so the node keeps animating and is never
// written to. Without a world the picture is transparent.
class Scene3DNode : public INode
{
public:
   static INode* Create() { return new Scene3DNode(); }
   Scene3DNode();
   ~Scene3DNode() override;

   enum class Kind { Missing, Geometry, Camera, Light };

   struct Item
   {
      int id = 0;
      uint64_t uid = 0;
      float pos[3] = { 0, 0, 0 };
      float rot[3] = { 0, 0, 0 };   // degrees, X then Y then Z
      float scale[3] = { 1, 1, 1 };
      bool visible = true;
      std::vector<uint64_t> unlit;  // light nodes this item ignores
   };

   // Set once by the app: the live node for a uid, nullptr when it is gone.
   static std::function<INode*(uint64_t)> ResolveUid;

   static Mat4 Placement(const Item& item);
   static Kind KindOf(const INode* node);

   // The model. Edits go through these so the saved text follows.
   std::vector<Item>& Items()
   {
      if (mText != mParsedText)
         Parse();
      return mItems;
   }
   Item* FindItem(int id);
   Item& AddItem(uint64_t uid);
   void RemoveItem(int id);
   void Commit();                    // call after changing an Item
   Kind ItemKind(const Item& item) const;

   int activeCamera = 0;             // item id; 0 = the default view
   uint64_t worldUid = 0;            // an Environment node, or any node with a picture
   bool worldColorOn = false;
   float worldColor[3] = { 0.04f, 0.04f, 0.06f };

   // INode
   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mRender.GetOutputWidth(); }
   int GetOutputHeight() const override { return mRender.GetOutputHeight(); }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }
   void VisitParams(ParamVisitor& v) override;

   // The editor's own render of the same scene from a free view. Separate from
   // the output so looking around never touches the picture, and only exists
   // once the panel asks.
   enum class Shading { Wire, Solid, Final };
   struct View
   {
      Mat4 view, proj;
      float eye[3] = { 0, 0, 0 };
      Shading shading = Shading::Solid;
      bool grid = true;
      int gridAxis = 1;       // the axis the grid plane faces
      float gridDist = 6.0f;  // how far the view stands back, which picks the grid's scale
   };
   unsigned int RenderView(int w, int h, const View& v);
   // The active camera's view as the output sees it.
   bool CameraView(Mat4& view, float eye[3], CameraNode** lens) const;
   // Any camera item's view.
   bool ItemCamera(const Item& item, Mat4& view, float eye[3], CameraNode** lens) const;
   // The placement that makes a camera item see `view`.
   void AlignToView(Item& item, const Mat4& view) const;
   // Render 3D's look settings, shared by the output and the Final view.
   Render3DNode& Look() { return mRender; }

   // World box of an item as last drawn in the editor.
   bool ItemBounds(int id, float lo[3], float hi[3]);
   // Position of a camera or light item, for its marker.
   bool ItemPoint(const Item& item, float out[3]) const;
   // Where a light item shines, as a unit vector.
   bool ItemLightAim(const Item& item, float aim[3]) const;
   size_t ClippedDraws() const { return mClipped; }

private:
   class Placed;
   void Parse();
   void Serialize();
   void Sync(int frameId);
   void ApplyWorld(Render3DNode& render, bool show);
   unsigned int Composite(unsigned int sceneTex, int w, int h, unsigned int backdrop, GLUtil::Fbo& out);
   unsigned int GridPass(unsigned int sceneTex, int w, int h, const View& v);

   std::vector<Item> mItems;
   int mNextId = 1;
   std::string mText;
   std::string mParsedText;

   // The one source Render 3D sees; its children are the placed items.
   class Root;
   std::unique_ptr<Root> mRoot;
   std::map<int, std::unique_ptr<Placed>> mPlaced;

   Render3DNode mRender;
   std::unique_ptr<Render3DNode> mEditor;
   Render3DNode::Rig mRig, mEditorRig;
   std::vector<float> mRigStamp, mEditorStamp;
   INode* mWorldNode = nullptr;
   GLUtil::Fbo mOutFbo, mEditorFbo, mGridFbo;
   unsigned int mCompositeProgram = 0;
   unsigned int mGridProgram = 0;
   unsigned int mOutTex = 0;
   unsigned long long mRevision = 0;
   unsigned long long mBuiltRevision = 0;
   unsigned long long mEditorRevision = 1;
   int mSyncedFrame = -1;
   int mLastCookFrame = -1;
   int mEditorFrame = 0;
   size_t mClipped = 0;
};
