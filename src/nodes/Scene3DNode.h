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
      // Delta transform: a second offset on top of the one above. Location adds, rotation turns
      // after it, scale multiplies; the tools move only the main one.
      float dpos[3] = { 0, 0, 0 };
      float drot[3] = { 0, 0, 0 };
      float dscale[3] = { 1, 1, 1 };
      bool visible = true;
      bool render = true;           // false: seen in the view, left out of the picture
      int lookAt = 0;               // camera or light only: item id it keeps facing, 0 = none
      unsigned lock = 0;            // bits 0-2 location, 3-5 rotation, 6-8 scale (engine axes): the transform tools leave them alone
      int group = 0;                // items with the same number select and move together; 0 = none
      std::string name;             // empty: the node's own name
      std::vector<uint64_t> unlit;  // light nodes this item ignores
   };

   // What the scene remembers beyond its items, saved with it.
   struct Settings
   {
      float fps = 60.0f;
      float seconds = 15.0f;
      float percent = 100.0f;       // resolution scale
      bool transparent = false;     // film: the world lights but is not seen
      bool snap = false;
      float snapMove = 0.25f, snapRot = 15.0f, snapScale = 0.1f;
      float passAlpha = 0.5f;
      bool safe = false, thirds = false, center = false, golden = false;
      float safeTitle = 0.035f, safeAction = 0.05f;
      int orientation = 0;          // 0 global, 1 local, 2 view
      int pivot = 0;                // 0 median, 1 individual, 2 active, 3 3D cursor
      float cursor[3] = { 0, 0, 0 };
      bool zoomToMouse = false;   // Blender's default: zooming never moves the orbit pivot
      bool orbitSelection = false;   // Blender's default: the view turns about its own centre
      int shadowItem = 0;           // the light that casts; 0 = the first one
      float fStop = 0.0f;           // 0 leaves the look's own aperture
      bool proportional = false;    // moving, turning or scaling also pulls the items near the selection
      float propSize = 2.0f;        // how far the pull reaches, in scene units
      bool snapObjects = false;     // a free move that comes near another item's origin jumps onto it
      bool invertZoom = false;      // the wheel zooms the other way
      bool textInfo = true, stats = false, originsAll = false;   // overlay texts and every item's origin
      bool gizmoMove = true, gizmoNav = true;   // the transform handles with the tool pad, and the view gizmo
      int propFalloff = 0;          // 0 smooth, 1 sphere, 2 root, 3 linear, 4 sharp, 5 constant
      // Render region, as fractions of the frame from the bottom left. Outside it the picture is
      // clear; crop makes the picture just the region.
      bool border = false, crop = false;
      float borderMin[2] = { 0, 0 }, borderMax[2] = { 1, 1 };
   };

   // Set once by the app: the live node for a uid, nullptr when it is gone.
   static std::function<INode*(uint64_t)> ResolveUid;

   static Mat4 Placement(const Item& item);
   // Sets the item's rotation so its whole turn, delta included, is `r` (rows of a rotation).
   static void SetTurn(Item& item, const float r[3][3]);
   // Moves the main transform into the delta and resets the main one (Blender's "Transforms to
   // Deltas"); the item stays where it is.
   static void TransformsToDeltas(Item& item);
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

   // The whole scene as saved, for duplicating and copying it.
   const std::string& SceneText() const { return mText; }
   void SetSceneText(const std::string& text) { mText = text; }

   bool panelOnly = false;           // lives in the 3D View panel until Convert to node
   int activeCamera = 0;             // item id; 0 = the default view
   uint64_t worldUid = 0;            // an Environment node, or any node with a picture
   bool worldColorOn = false;
   float worldColor[3] = { 0.04f, 0.04f, 0.06f };
   float worldStrength = 1.0f;       // the colour lights the scene as well as filling it
   int focusItem = 0;                // item id the camera stays focused on; 0 = the focus distance
   Settings settings;
   float renderW = 1920.0f, renderH = 1080.0f;   // before the resolution scale

   // INode
   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override { return mOutW > 0 ? mOutW : mRender.GetOutputWidth(); }
   int GetOutputHeight() const override { return mOutH > 0 ? mOutH : mRender.GetOutputHeight(); }
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
      std::vector<int> only;  // local view: just these items
   };
   unsigned int RenderView(int w, int h, const View& v);
   // The active camera's view as the output sees it.
   bool CameraView(Mat4& view, float eye[3], CameraNode** lens) const;
   // Any camera item's view.
   bool ItemCamera(const Item& item, Mat4& view, float eye[3], CameraNode** lens) const;
   // The placement that makes a camera item see `view`.
   void AlignToView(Item& item, const Mat4& view) const;
   // Turns a camera or light about its own point until it looks at `target`.
   bool AimAt(Item& item, const float target[3]) const;
   // Render 3D's look settings, shared by the output and the Final view.
   Render3DNode& Look() { return mRender; }

   // World box of an item as last drawn in the editor.
   bool ItemBounds(int id, float lo[3], float hi[3]);
   // Position of a camera or light item, for its marker.
   bool ItemPoint(const Item& item, float out[3]) const;
   // Where an item stands: a marker's point, a box's middle, else its position.
   void ItemCenter(const Item& item, float out[3]);
   // Where a light item shines, as a unit vector.
   bool ItemLightAim(const Item& item, float aim[3]) const;
   size_t ClippedDraws() const { return mClipped; }

private:
   class Placed;
   void Parse();
   const Item* FindConst(int id) const;
   void Serialize();
   void Sync(int frameId);
   void ApplyWorld(Render3DNode& render, bool show, bool background);
   unsigned int Composite(unsigned int sceneTex, int w, int h, unsigned int backdrop, GLUtil::Fbo& out);
   unsigned int GridPass(unsigned int sceneTex, int w, int h, const View& v);

   std::vector<Item> mItems;
   int mNextId = 1;
   std::string mText;
   std::string mParsedText;

   // The one source Render 3D sees; its children are the placed items.
   class Root;
   std::unique_ptr<Root> mRoot, mOutRoot, mLocalRoot;
   std::map<int, std::unique_ptr<Placed>> mPlaced;

   Render3DNode mRender;
   std::unique_ptr<Render3DNode> mEditor;
   Render3DNode::Rig mRig, mOutRig, mEditorRig;
   std::vector<float> mRigStamp, mEditorStamp;
   INode* mWorldNode = nullptr;
   GLUtil::Fbo mOutFbo, mEditorFbo, mGridFbo;
   unsigned int mCompositeProgram = 0;
   GLUtil::Fbo mBorderFbo;
   unsigned int mBorderProgram = 0;
   int mOutW = 0, mOutH = 0;         // the picture's size after a crop; 0 = the render's
   unsigned int Border(unsigned int sceneTex, int w, int h);
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
