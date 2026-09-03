#pragma once

#include "core/INode.h"
#include "field/FieldGraphHost.h"
#include "field/FieldGraphKernel.h"
#include "field/FieldGraphOwnership.h"
#include "field/FieldIR.h"
#include "field/ParamTable.h"

#include <string>

// Field 'graph' domain (build step 10): a kernel that runs once, at edit
// time, and emits/wires/configures real Infinite nodes (main.cpp §MainGraphHost
// drives the real graph; see FieldGraphReconciler.h for the diff and
// FieldGraphKernel.h for the interpreter). Main-thread-only, edit-time-only -
// no audio-thread object, no CookIfNeeded, unlike the other three Field node
// types.
class FieldGraphNode : public INode
{
public:
   static INode* Create() { return new FieldGraphNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int /*frameId*/) override {}
   void VisitParams(ParamVisitor& v) override;

   // Lex -> parse -> LowerGraphProgramToIR only. Populates mLastError/
   // mLastProgram on success. Never mutates the graph - safe to call from
   // any path that just needs the node's compiled state refreshed (paste,
   // patch load, undo/redo - see main.cpp's ReloadDerivedState, T11).
   bool Apply();

   // Compiles if needed (Apply()), then interprets the program and
   // reconciles the result against the live graph via `host`. This is the
   // only path that actually mounts/unmounts/wires nodes - it must only be
   // called from the main thread, outside the ImGui draw pass proper (see
   // doc trap T14), and wraps its own undo checkpoint (doc §5.4).
   bool Regenerate(Field::IFieldGraphHost& host);

   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }

   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }

   const Field::GraphOwnershipMap& Ownership() const { return mOwnership; }
   Field::GraphOwnershipMap& Ownership() { return mOwnership; }

   // 16 lowercase hex chars, regenerated only when this node's identity
   // must diverge from a source node's (paste - doc §5.7.3), never on an
   // ordinary compile/regenerate.
   const std::string& Uid() const { return mUid; }
   void SetUid(const std::string& uid) { mUid = uid; }
   static std::string NewUid();

   bool DrivesParam(int targetNodeIndex, const std::string& paramName) const
   {
      for (const auto& s : mLastPlan.sets)
      {
         if (s.paramName == paramName && mOwnership.Get(s.targetKey) == targetNodeIndex)
            return true;
      }
      return false;
   }

   std::string code =
      "# emit(\"Type Name\", k0, k1, ...) -> handle\n"
      "# connect(srcHandle, srcSlot, dstHandle, dstSlot)\n"
      "# set(handle, \"paramName\", value)\n"
      "# place(handle, x, y)\n";
   std::string ownershipText; // mirrors mOwnership.ToText(), synced by VisitParams

private:
   Field::ParamTable mParamTable;
   Field::GraphIRProgram mLastProgram;
   Field::GraphPlan mLastPlan;
   Field::GraphOwnershipMap mOwnership;
   std::string mUid;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   bool mHasCompiledProgram = false;
};
