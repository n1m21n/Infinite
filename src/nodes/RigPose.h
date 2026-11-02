#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "INode.h"
#include "Skinning.h"

// What a hand or a modulator adds on top of a rig, shared by every rigged
// node (Model 3D's glTF skin, Armature's bone chain): 16 pose slots turned in
// the 3D View's Pose mode, and one IK goal. Every value is a param, saved
// under the same keys on every node.
struct RigPose
{
   static constexpr int kSlots = 16;

   // Each slot turns one joint in its own frame by X/Y/Z degrees, after the
   // rig's own motion (clip, curl, wave). -1 = unused.
   int poseJoint[kSlots] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
   float poseRot[kSlots][3] = {};

   // IK (Blender's IK constraint): the head of ikJoint reaches for the
   // target by turning the ikChain joints above it; ikWeight blends it in.
   // Target and pole are in the node's own mesh space. A 3D View item set as
   // the target writes them every frame (Scene3DNode).
   bool ikOn = false;
   int ikJoint = -1;
   int ikChain = 2;
   float ikWeight = 1.0f;
   float ikTarget[3] = { 0.0f, 0.0f, 0.0f };
   bool ikPoleOn = false;
   float ikPole[3] = { 0.0f, 0.0f, 1.0f };

   void Visit(ParamVisitor& v)
   {
      static const char* const kKeys[kSlots][4] = {
         { "pose0", "pose0X", "pose0Y", "pose0Z" }, { "pose1", "pose1X", "pose1Y", "pose1Z" },
         { "pose2", "pose2X", "pose2Y", "pose2Z" }, { "pose3", "pose3X", "pose3Y", "pose3Z" },
         { "pose4", "pose4X", "pose4Y", "pose4Z" }, { "pose5", "pose5X", "pose5Y", "pose5Z" },
         { "pose6", "pose6X", "pose6Y", "pose6Z" }, { "pose7", "pose7X", "pose7Y", "pose7Z" },
         { "pose8", "pose8X", "pose8Y", "pose8Z" }, { "pose9", "pose9X", "pose9Y", "pose9Z" },
         { "pose10", "pose10X", "pose10Y", "pose10Z" }, { "pose11", "pose11X", "pose11Y", "pose11Z" },
         { "pose12", "pose12X", "pose12Y", "pose12Z" }, { "pose13", "pose13X", "pose13Y", "pose13Z" },
         { "pose14", "pose14X", "pose14Y", "pose14Z" }, { "pose15", "pose15X", "pose15Y", "pose15Z" } };
      for (int i = 0; i < kSlots; i++)
      {
         v.Int(kKeys[i][0], poseJoint[i]);
         for (int a = 0; a < 3; a++)
            v.Float(kKeys[i][a + 1], poseRot[i][a]);
      }
      v.Bool("ikOn", ikOn); v.Int("ikJoint", ikJoint); v.Int("ikChain", ikChain);
      v.Float("ikWeight", ikWeight);
      v.Float("ikX", ikTarget[0]); v.Float("ikY", ikTarget[1]); v.Float("ikZ", ikTarget[2]);
      v.Bool("ikPoleOn", ikPoleOn);
      v.Float("ikPoleX", ikPole[0]); v.Float("ikPoleY", ikPole[1]); v.Float("ikPoleZ", ikPole[2]);
   }

   void Clear()
   {
      *this = RigPose();
   }

   // The slot holding joint j, or -1. With `make`, takes a free slot (or -1
   // when all are used).
   int Slot(int j, bool make)
   {
      for (int i = 0; i < kSlots; i++)
         if (poseJoint[i] == j)
            return i;
      if (!make)
         return -1;
      for (int i = 0; i < kSlots; i++)
         if (poseJoint[i] < 0)
         {
            poseJoint[i] = j;
            std::fill(poseRot[i], poseRot[i] + 3, 0.0f);
            return i;
         }
      return -1;
   }

   bool IkActive(int jointCount) const
   {
      return ikOn && ikJoint >= 0 && ikJoint < jointCount && ikWeight > 0.0f;
   }

   // Everything that changes the posed result, for a node's "re-pose?" key.
   void AppendKey(std::vector<float>& key, int jointCount) const
   {
      for (int i = 0; i < kSlots; i++)
      {
         key.push_back((float)poseJoint[i]);
         key.insert(key.end(), poseRot[i], poseRot[i] + 3);
      }
      if (IkActive(jointCount))
      {
         key.insert(key.end(), { (float)ikJoint, (float)ikChain, ikWeight, (float)ikPoleOn });
         key.insert(key.end(), ikTarget, ikTarget + 3);
         key.insert(key.end(), ikPole, ikPole + 3);
      }
   }

   // Multiplies the slot turns into per-joint offsets (4 floats a joint;
   // filled with identity first when empty).
   void ApplySlots(std::vector<float>& offsets, int jointCount) const
   {
      for (int i = 0; i < kSlots; i++)
      {
         if (poseJoint[i] < 0 || poseJoint[i] >= jointCount)
            continue;
         if (offsets.empty())
         {
            offsets.assign(jointCount * 4, 0.0f);
            for (int j = 0; j < jointCount; j++)
               offsets[j * 4 + 3] = 1.0f;
         }
         float q[4], combined[4];
         Skinning::QuatFromEuler(poseRot[i], q);
         Skinning::QuatMul(&offsets[poseJoint[i] * 4], q, combined);
         std::copy(combined, combined + 4, &offsets[poseJoint[i] * 4]);
      }
   }

   // The IK goal in the rig's own space, given the node's fit (mesh space =
   // (rig space - centre) * scale). False when IK is off.
   bool Goal(int jointCount, const float centre[3], float scale, Skinning::IkGoal& goal) const
   {
      if (!IkActive(jointCount))
         return false;
      const float inv = scale != 0.0f ? 1.0f / scale : 1.0f;
      goal.joint = ikJoint;
      goal.chain = std::clamp(ikChain, 1, 8);
      goal.weight = std::clamp(ikWeight, 0.0f, 1.0f);
      goal.usePole = ikPoleOn;
      for (int k = 0; k < 3; k++)
      {
         goal.target[k] = ikTarget[k] * inv + centre[k];
         goal.pole[k] = ikPole[k] * inv + centre[k];
      }
      return true;
   }
};

// A node the 3D View's Pose mode can pose: Model 3D with a skin, Armature.
// Joint matrices are in the node's own mesh space (what GetMesh returns, before
// GetModelMatrix), column-major.
class IRigged
{
public:
   virtual ~IRigged() {}
   virtual bool Poseable() const = 0;
   virtual int JointCount() const = 0;
   virtual const std::string& JointName(int j) const = 0;
   virtual int JointParent(int j) const = 0;
   virtual bool JointMatrix(int j, float out[16]) = 0;
   virtual RigPose& Pose() = 0;
};
