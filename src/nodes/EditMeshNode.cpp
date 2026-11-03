#include "EditMeshNode.h"

#include <algorithm>


namespace
{
   const Mesh kEmpty;
}

void EditMeshNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   Update();
}

void EditMeshNode::Update()
{
   if (input == nullptr)
   {
      mCookWarning.clear();
      mMesh = Mesh();
      mSelection = EditMesh::Selection();
      mKeyInput = nullptr;
      mBuilt = false;
      return;
   }
   const unsigned long long rev = input->MeshRevision();
   if (mBuilt && input == mKeyInput && rev == mKeyRevision && ops == mKeyOps)
      return;
   mBuilt = true;
   mKeyInput = input;
   mKeyRevision = rev;
   mKeyOps = ops;
   int missed = 0;
   mMesh = EditMesh::Apply(input->GetMesh(), ops, &missed, &mSelection);
   mCookWarning = missed > 0 ? std::to_string(missed) + (missed == 1 ? " selection" : " selections") + " not found"
                             : std::string();
   mMeshRevision = NextMeshRevision();
}

const Mesh& EditMeshNode::GetMesh()
{
   if (input == nullptr)
      return kEmpty;
   Update();
   return mMesh;
}

unsigned long long EditMeshNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   Update();
   return mMeshRevision;
}

int EditMeshNode::OpCount() const
{
   int n = 0;
   size_t at = 0;
   while (at < ops.size())
   {
      size_t end = ops.find('\n', at);
      if (end == std::string::npos)
         end = ops.size();
      if (ops.find_first_not_of(" \t\r", at) < end && ops.compare(at, 4, "sel ") != 0)
         n++;
      at = end + 1;
   }
   return n;
}
