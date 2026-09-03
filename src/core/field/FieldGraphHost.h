#pragma once

#include <string>

// Field 'graph' domain (build step 10): the seam between the reconciler
// (pure diff logic, testable without ImGui/GL/audio) and the real Infinite
// graph (gNodes, SpawnNode, RemoveNodeByIndex, ...). main.cpp provides the
// real implementation (MainGraphHost); tests provide a fake one. No
// GraphNode/ImGui/gNodes reference belongs in this header.
namespace Field
{
   class IFieldGraphHost
   {
   public:
      virtual ~IFieldGraphHost() = default;

      // Spawns a node of `typeName` and returns an opaque host-assigned id
      // (e.g. a node index) to identify it in later calls this same pass.
      // Returns -1 on failure (typeName not spawnable/not found).
      virtual int Mount(const std::string& typeName) = 0;

      // Removes the node identified by `id` (previously returned by Mount,
      // or read back from the persisted ownership map).
      virtual void Unmount(int id) = 0;

      virtual void SetParam(int id, const std::string& paramName, float value) = 0;

      virtual void Connect(int srcId, int srcSlot, int dstId, int dstSlot) = 0;

      virtual void Place(int id, float x, float y) = 0;

      // True if a node with this id still exists in the live graph (an
      // ownership entry can go stale if the user hand-deleted the node -
      // see doc §5.7).
      virtual bool Alive(int id) const = 0;

      // The live node's type name, or "" if `id` is not alive. Used to
      // decide MOUNT/UPDATE vs. REMOUNT when a re-run's emit() type for a
      // given identity key differs from what's already mounted there.
      virtual std::string TypeNameOf(int id) const = 0;

      // True if `typeName` names a real, user-spawnable node type.
      virtual bool Spawnable(const std::string& typeName) const = 0;

      // Remounts the node identified by `existing` with fresh type `typeName`.
      // Default implementation unmounts and mounts afresh. A real host can rescue
      // group membership, cables, and modulation links across the transition.
      virtual int Remount(int existing, const std::string& typeName)
      {
         Unmount(existing);
         return Mount(typeName);
      }

      virtual int DroppedModCount() const { return 0; }
      virtual int DetachedCableCount() const { return 0; }
   };
}
